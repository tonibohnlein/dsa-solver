// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/reuse_penalty_baseline_solvers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dsa/algorithms/placement_engine.h"
#include "dsa/algorithms/reuse_penalty_search.h"
#include "dsa/algorithms/solver.h"
#include "dsa/model/model.h"
#include "dsa/model/validator.h"

namespace dsa {
namespace {

using BaselineProblem = detail::ReusePenaltySearchSpace;
using NodePair = detail::ReuseNodePair;
using Score = std::vector<std::uint64_t>;

SolverCapabilities BaselineCapabilities() {
  SolverCapabilities capabilities;
  capabilities.multi_interval = true;
  capabilities.reuse_cost = true;
  capabilities.colocations = true;
  capabilities.separations = true;
  capabilities.temporal_exclusions = true;
  capabilities.reserved_ranges = true;
  capabilities.multi_pool = true;
  capabilities.lexicographic_objective = true;
  capabilities.capacity_objective = true;
  capabilities.peak_objective = true;
  return capabilities;
}

DsaResult RejectUnsupported(const DsaProblem& problem, const char* solver_name,
                            const SolverCapabilities& capabilities) {
  DsaResult result;
  const std::vector<std::string> validation = ValidateProblem(problem);
  if (!validation.empty()) {
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = validation;
    return result;
  }
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, capabilities);
  result.status = SolveStatus::kUnsupported;
  for (const std::string& feature : compatibility.unsupported_features) {
    result.diagnostics.push_back(std::string(solver_name) + " does not support feature '" +
                                 feature + "'");
  }
  for (const std::string& objective : compatibility.unsupported_objectives) {
    result.diagnostics.push_back(std::string(solver_name) + " does not support objective '" +
                                 objective + "'");
  }
  return result;
}

std::vector<std::size_t> DefaultNodeOrder(const DsaProblem& problem,
                                          const BaselineProblem& prepared) {
  return detail::DefaultReuseNodeOrder(problem, prepared);
}

std::map<NodePair, std::uint64_t> ActiveSoftEdges(const BaselineProblem& prepared) {
  return prepared.soft_weights;
}

std::set<NodePair> ActivePairs(const std::map<NodePair, std::uint64_t>& active) {
  std::set<NodePair> promoted;
  for (const auto& [pair, weight] : active) {
    static_cast<void>(weight);
    promoted.insert(pair);
  }
  return promoted;
}

Score ObjectiveScore(const DsaProblem& problem, const ObjectiveValue& objective) {
  Score score;
  score.reserve(problem.objective.terms.size());
  for (ObjectiveMetric metric : problem.objective.terms) {
    score.push_back(EvaluateObjectiveMetric(problem, objective, metric));
  }
  return score;
}

std::optional<NodePair> CheapestSoftEdgeOnOverflowChain(
    const DsaProblem& promoted_problem, const BaselineProblem& prepared,
    const DsaSolution& solution, const std::map<NodePair, std::uint64_t>& active) {
  const detail::PlacementSearchSpace promoted_search =
      detail::BuildPlacementSearchSpace(promoted_problem);
  if (!promoted_search.errors.empty() ||
      promoted_search.nodes.size() != prepared.placement.nodes.size()) {
    return std::nullopt;
  }

  std::unordered_map<BufferId, std::size_t> promoted_by_representative;
  for (std::size_t index = 0; index < promoted_search.nodes.size(); ++index) {
    promoted_by_representative.emplace(promoted_search.nodes[index].representative, index);
  }
  std::optional<std::size_t> current;
  std::uint64_t largest_overflow = 0;
  std::uint64_t largest_top = 0;
  for (std::size_t index = 0; index < promoted_search.nodes.size(); ++index) {
    const detail::PlacementSearchNode& node = promoted_search.nodes[index];
    const Placement* placement = solution.Find(node.representative);
    const Pool* pool = promoted_problem.FindPool(node.pool);
    if (placement == nullptr || pool == nullptr || !pool->capacity ||
        detail::AddOverflows(placement->offset, node.size)) {
      continue;
    }
    const std::uint64_t top = placement->offset + node.size;
    if (top <= *pool->capacity) continue;
    const std::uint64_t overflow = top - *pool->capacity;
    if (!current || overflow > largest_overflow ||
        (overflow == largest_overflow && top > largest_top) ||
        (overflow == largest_overflow && top == largest_top &&
         node.representative < promoted_search.nodes[*current].representative)) {
      current = index;
      largest_overflow = overflow;
      largest_top = top;
    }
  }
  if (!current) return std::nullopt;

  std::vector<NodePair> soft_on_chain;
  std::set<std::size_t> visited;
  while (visited.insert(*current).second) {
    const detail::PlacementSearchNode& node = promoted_search.nodes[*current];
    const Placement* placement = solution.Find(node.representative);
    if (placement == nullptr || placement->offset == 0) break;
    std::optional<std::size_t> support;
    for (BufferId neighbor_id : node.conflicts) {
      const auto found = promoted_by_representative.find(neighbor_id);
      if (found == promoted_by_representative.end()) continue;
      const std::size_t neighbor = found->second;
      const detail::PlacementSearchNode& neighbor_node = promoted_search.nodes[neighbor];
      const Placement* neighbor_placement = solution.Find(neighbor_node.representative);
      if (neighbor_placement == nullptr || neighbor_placement->pool != placement->pool ||
          detail::AddOverflows(neighbor_placement->offset, neighbor_node.size) ||
          neighbor_placement->offset + neighbor_node.size != placement->offset) {
        continue;
      }
      if (!support ||
          neighbor_node.representative < promoted_search.nodes[*support].representative) {
        support = neighbor;
      }
    }
    if (!support) break;
    const auto current_prepared =
        prepared.node_by_buffer.find(promoted_search.nodes[*current].representative);
    const auto support_prepared =
        prepared.node_by_buffer.find(promoted_search.nodes[*support].representative);
    if (current_prepared == prepared.node_by_buffer.end() ||
        support_prepared == prepared.node_by_buffer.end()) {
      break;
    }
    const NodePair pair =
        detail::CanonicalNodePair(current_prepared->second, support_prepared->second);
    if (active.count(pair) != 0) soft_on_chain.push_back(pair);
    current = support;
  }
  if (soft_on_chain.empty()) return std::nullopt;
  return *std::min_element(soft_on_chain.begin(), soft_on_chain.end(),
                           [&](const NodePair& first, const NodePair& second) {
                             return std::tie(active.at(first), first) <
                                    std::tie(active.at(second), second);
                           });
}

}  // namespace

const char* CanonicalGreedySolver::Name() const noexcept { return "canonical_greedy"; }

SolverCapabilities CanonicalGreedySolver::Capabilities() const noexcept {
  return BaselineCapabilities();
}

DsaResult CanonicalGreedySolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    return RejectUnsupported(problem, Name(), Capabilities());
  }
  for (const Pool& pool : problem.pools) {
    if (!pool.capacity) {
      DsaResult result;
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.emplace_back("canonical_greedy requires fixed pool capacities");
      return result;
    }
  }

  DsaResult error;
  const BaselineProblem prepared = detail::BuildReusePenaltySearchSpace(problem);
  if (!prepared.placement.errors.empty()) {
    error.status = SolveStatus::kInvalidProblem;
    error.diagnostics = prepared.placement.errors;
    return error;
  }
  if (prepared.placement.flexible_pools) {
    error.status = SolveStatus::kUnsupported;
    error.diagnostics.emplace_back("DSA-RP baselines do not support flexible pool assignment");
    return error;
  }
  const std::vector<std::size_t> order = DefaultNodeOrder(problem, prepared);
  if (order.size() != prepared.placement.nodes.size()) {
    error.status = SolveStatus::kInvalidProblem;
    error.diagnostics.emplace_back("canonical_greedy could not construct a complete buffer order");
    return error;
  }

  std::vector<bool> placed(prepared.placement.nodes.size(), false);
  std::vector<std::uint64_t> offsets(prepared.placement.nodes.size(), 0);
  std::uint64_t evaluated = 0;
  for (std::size_t current : order) {
    const detail::PlacementSearchNode& node = prepared.placement.nodes[current];
    const std::set<std::uint64_t> candidates =
        detail::CanonicalOffsetsForNode(problem, prepared, placed, offsets, current, true);

    std::optional<std::tuple<std::uint64_t, std::uint64_t>> best;
    for (std::uint64_t offset : candidates) {
      ++evaluated;
      if (!detail::FitsReuseNodeAt(problem, prepared, placed, offsets, current, offset)) {
        continue;
      }
      const auto score = std::make_tuple(
          detail::IncrementalReuseCost(prepared, placed, offsets, current, offset), offset);
      if (!best || score < *best) best = score;
    }
    if (!best) {
      DsaResult result;
      result.status = SolveStatus::kBestEffortNoFit;
      result.diagnostics.push_back(
          "canonical_greedy found no capacity-fitting canonical offset for buffer " +
          std::to_string(node.representative));
      result.solver_metrics = {{"candidate_offsets_evaluated", evaluated}};
      return result;
    }
    offsets[current] = std::get<1>(*best);
    placed[current] = true;
  }

  DsaResult result =
      detail::BuildValidatedReuseResult(problem, prepared, offsets, SolveStatus::kFeasible);
  result.solver_metrics = {
      {"candidate_offsets_evaluated", evaluated},
      {"soft_edges", prepared.soft_weights.size()},
  };
  result.diagnostics.emplace_back(
      "canonical_greedy uses the hard-or-soft support menu and selects by incremental "
      "reuse cost, then offset");
  return result;
}

const char* PromoteRepairSolver::Name() const noexcept { return "promote_repair"; }

SolverCapabilities PromoteRepairSolver::Capabilities() const noexcept {
  return BaselineCapabilities();
}

DsaResult PromoteRepairSolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    return RejectUnsupported(problem, Name(), Capabilities());
  }
  for (const Pool& pool : problem.pools) {
    if (!pool.capacity) {
      DsaResult result;
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.emplace_back("promote_repair requires fixed pool capacities");
      return result;
    }
  }

  DsaResult error;
  const BaselineProblem prepared = detail::BuildReusePenaltySearchSpace(problem);
  if (!prepared.placement.errors.empty()) {
    error.status = SolveStatus::kInvalidProblem;
    error.diagnostics = prepared.placement.errors;
    return error;
  }
  if (prepared.placement.flexible_pools) {
    error.status = SolveStatus::kUnsupported;
    error.diagnostics.emplace_back("DSA-RP baselines do not support flexible pool assignment");
    return error;
  }
  std::map<NodePair, std::uint64_t> active = ActiveSoftEdges(prepared);
  std::uint64_t attempts = 0;
  std::uint64_t demotions = 0;
  DsaResult last;
  while (true) {
    const DsaProblem promoted =
        detail::WithPromotedReuseEdges(problem, prepared, ActivePairs(active));
    last = detail::PlaceWithOrder(promoted, {});
    ++attempts;
    if (last.status == SolveStatus::kFeasible && last.solution.has_value()) {
      last.objective = EvaluateObjective(problem, last.solution.value());
      last.solver_metrics = {
          {"initial_soft_edges", prepared.soft_weights.size()},
          {"active_soft_edges", active.size()},
          {"demoted_edges", demotions},
          {"packing_attempts", attempts},
      };
      last.diagnostics.emplace_back(
          "promote_repair decoded all active soft edges as separations and repaired "
          "over-capacity support chains");
      return last;
    }
    if (!last.solution || active.empty()) break;
    const std::optional<NodePair> demote =
        CheapestSoftEdgeOnOverflowChain(promoted, prepared, *last.solution, active);
    if (!demote) break;
    active.erase(*demote);
    ++demotions;
  }

  DsaResult fallback = CanonicalGreedySolver().Solve(problem);
  if (fallback.status == SolveStatus::kFeasible) {
    fallback.solver_metrics["initial_soft_edges"] = prepared.soft_weights.size();
    fallback.solver_metrics["demoted_edges"] = demotions;
    fallback.solver_metrics["packing_attempts"] = attempts;
    fallback.solver_metrics["fallback_to_canonical_greedy"] = 1;
    fallback.diagnostics.emplace_back(
        "promote_repair encountered a hard-only support chain and fell back to "
        "canonical_greedy");
    return fallback;
  }
  last.solver_metrics = {
      {"initial_soft_edges", prepared.soft_weights.size()},
      {"active_soft_edges", active.size()},
      {"demoted_edges", demotions},
      {"packing_attempts", attempts},
      {"fallback_to_canonical_greedy", 1},
  };
  last.diagnostics.emplace_back(
      "promote_repair could not repair the decoded placement within capacity");
  return last;
}

const char* PromoteAllSolver::Name() const noexcept { return "promote_all"; }

SolverCapabilities PromoteAllSolver::Capabilities() const noexcept {
  return BaselineCapabilities();
}

DsaResult PromoteAllSolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    return RejectUnsupported(problem, Name(), Capabilities());
  }
  const BaselineProblem prepared = detail::BuildReusePenaltySearchSpace(problem);
  if (!prepared.placement.errors.empty()) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = prepared.placement.errors;
    return result;
  }
  const DsaProblem promoted =
      detail::WithPromotedReuseEdges(problem, prepared, ActivePairs(ActiveSoftEdges(prepared)));
  DsaResult result = detail::PlaceWithOrder(promoted, {});
  if (result.solution.has_value()) {
    result.objective = EvaluateObjective(problem, result.solution.value());
  }
  result.solver_metrics = {
      {"promoted_edges", prepared.soft_weights.size()},
  };
  result.diagnostics.emplace_back(
      "promote_all treats every soft edge as a separation and does not "
      "repair an over-capacity placement");
  return result;
}

UnitRandomColoringSolver::UnitRandomColoringSolver(UnitRandomColoringOptions options)
    : options_(options) {}

const char* UnitRandomColoringSolver::Name() const noexcept { return "unit_random_coloring"; }

SolverCapabilities UnitRandomColoringSolver::Capabilities() const noexcept {
  SolverCapabilities capabilities = BaselineCapabilities();
  capabilities.reserved_ranges = false;
  return capabilities;
}

DsaResult UnitRandomColoringSolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    return RejectUnsupported(problem, Name(), Capabilities());
  }
  if (options_.samples == 0) {
    DsaResult result;
    result.status = SolveStatus::kUnsupported;
    result.diagnostics.emplace_back("unit_random_coloring requires at least one sample");
    return result;
  }
  const BaselineProblem prepared = detail::BuildReusePenaltySearchSpace(problem);
  if (!prepared.placement.errors.empty()) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = prepared.placement.errors;
    return result;
  }
  for (const auto& neighbors : prepared.hard_neighbors) {
    if (!neighbors.empty()) {
      DsaResult result;
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.emplace_back("unit_random_coloring requires a geometry-free hard graph");
      return result;
    }
  }

  std::map<PoolId, std::pair<std::uint64_t, std::uint64_t>> pool_shape;
  for (const Pool& pool : problem.pools) {
    if (!pool.capacity.has_value() || !pool.reserved_ranges.empty()) {
      DsaResult result;
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.emplace_back("unit_random_coloring requires fixed unreserved capacities");
      return result;
    }
  }
  for (const detail::PlacementSearchNode& node : prepared.placement.nodes) {
    auto& [unit, colors] = pool_shape[node.pool];
    if (unit == 0) {
      unit = node.size;
      const Pool* pool = problem.FindPool(node.pool);
      if (pool == nullptr || !pool->capacity.has_value() || unit == 0 ||
          *pool->capacity % unit != 0) {
        DsaResult result;
        result.status = SolveStatus::kUnsupported;
        result.diagnostics.emplace_back("unit_random_coloring requires integral unit capacities");
        return result;
      }
      colors = *pool->capacity / unit;
    }
    if (node.size != unit || unit % node.alignment != 0 || colors == 0) {
      DsaResult result;
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.emplace_back(
          "unit_random_coloring requires uniform unit sizes and compatible "
          "alignment");
      return result;
    }
  }

  std::mt19937_64 random(options_.seed);
  std::optional<DsaResult> best;
  Score best_score;
  for (std::size_t sample = 0; sample < options_.samples; ++sample) {
    std::vector<std::uint64_t> offsets(prepared.placement.nodes.size(), 0);
    for (std::size_t node = 0; node < prepared.placement.nodes.size(); ++node) {
      const auto [unit, colors] = pool_shape.at(prepared.placement.nodes[node].pool);
      offsets[node] = (random() % colors) * unit;
    }
    DsaResult candidate =
        detail::BuildValidatedReuseResult(problem, prepared, offsets, SolveStatus::kFeasible);
    if (candidate.status != SolveStatus::kFeasible) {
      return candidate;
    }
    const Score score = ObjectiveScore(problem, candidate.objective);
    if (!best.has_value() || score < best_score) {
      best = std::move(candidate);
      best_score = score;
    }
  }
  if (!best.has_value()) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics.emplace_back("unit_random_coloring did not evaluate a sample");
    return result;
  }
  DsaResult result = std::move(best).value();
  result.solver_metrics = {
      {"samples", options_.samples},
      {"seed", options_.seed},
  };
  result.diagnostics.emplace_back(
      "unit_random_coloring samples independent uniform address colors on "
      "the geometry-free unit-size slice");
  return result;
}

}  // namespace dsa

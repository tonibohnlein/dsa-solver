// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/reuse_penalty_baseline_solvers.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dsa/algorithms/canonical_exact_search.h"
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
  capabilities.peak_objective = false;
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

std::int64_t EarliestBirth(const DsaProblem& problem, const detail::PlacementSearchNode& node) {
  std::int64_t birth = std::numeric_limits<std::int64_t>::max();
  for (BufferId member : node.members) {
    const Buffer* buffer = problem.FindBuffer(member);
    if (buffer == nullptr) continue;
    for (const Interval& interval : buffer->live_intervals) {
      birth = std::min(birth, interval.lower);
    }
  }
  return birth;
}

std::vector<std::vector<std::size_t>> CanonicalGreedyOrders(const DsaProblem& problem,
                                                            const BaselineProblem& prepared,
                                                            const CanonicalGreedyOptions& options) {
  std::vector<std::vector<std::size_t>> orders;
  orders.push_back(DefaultNodeOrder(problem, prepared));

  std::vector<std::size_t> nodes(prepared.placement.nodes.size());
  for (std::size_t index = 0; index < nodes.size(); ++index) nodes[index] = index;
  std::sort(nodes.begin(), nodes.end(), [&](std::size_t first, std::size_t second) {
    return std::make_tuple(EarliestBirth(problem, prepared.placement.nodes[first]),
                           prepared.placement.nodes[first].representative) <
           std::make_tuple(EarliestBirth(problem, prepared.placement.nodes[second]),
                           prepared.placement.nodes[second].representative);
  });
  orders.push_back(nodes);

  std::vector<std::uint64_t> incident_weight(nodes.size(), 0);
  for (const auto& [pair, weight] : prepared.soft_weights) {
    incident_weight[pair.first] = detail::SaturatingAdd(incident_weight[pair.first], weight);
    incident_weight[pair.second] = detail::SaturatingAdd(incident_weight[pair.second], weight);
  }
  std::sort(nodes.begin(), nodes.end(), [&](std::size_t first, std::size_t second) {
    return std::make_tuple(
               std::numeric_limits<std::uint64_t>::max() - incident_weight[first],
               std::numeric_limits<std::uint64_t>::max() - prepared.placement.nodes[first].size,
               prepared.placement.nodes[first].representative) <
           std::make_tuple(
               std::numeric_limits<std::uint64_t>::max() - incident_weight[second],
               std::numeric_limits<std::uint64_t>::max() - prepared.placement.nodes[second].size,
               prepared.placement.nodes[second].representative);
  });
  orders.push_back(nodes);

  std::mt19937_64 random(options.seed);
  for (std::size_t restart = 0; restart < options.random_restarts; ++restart) {
    std::shuffle(nodes.begin(), nodes.end(), random);
    orders.push_back(nodes);
  }

  std::vector<std::vector<std::size_t>> unique;
  for (std::vector<std::size_t>& order : orders) {
    if (std::find(unique.begin(), unique.end(), order) == unique.end()) {
      unique.push_back(std::move(order));
    }
  }
  return unique;
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
    std::uint64_t support_top = 0;
    for (BufferId neighbor_id : node.conflicts) {
      const auto found = promoted_by_representative.find(neighbor_id);
      if (found == promoted_by_representative.end()) continue;
      const std::size_t neighbor = found->second;
      const detail::PlacementSearchNode& neighbor_node = promoted_search.nodes[neighbor];
      const Placement* neighbor_placement = solution.Find(neighbor_node.representative);
      if (neighbor_placement == nullptr || neighbor_placement->pool != placement->pool ||
          detail::AddOverflows(neighbor_placement->offset, neighbor_node.size)) {
        continue;
      }
      const std::uint64_t neighbor_top = neighbor_placement->offset + neighbor_node.size;
      const std::optional<std::uint64_t> aligned_top =
          detail::AlignUp(neighbor_top, node.alignment);
      if (!aligned_top.has_value() || aligned_top.value() != placement->offset) {
        continue;
      }
      if (!support || neighbor_top > support_top ||
          (neighbor_top == support_top &&
           neighbor_node.representative < promoted_search.nodes[*support].representative)) {
        support = neighbor;
        support_top = neighbor_top;
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

CanonicalGreedySolver::CanonicalGreedySolver(CanonicalGreedyOptions options) : options_(options) {}

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
  const std::vector<std::vector<std::size_t>> orders =
      CanonicalGreedyOrders(problem, prepared, options_);
  DsaResult result;
  Score best_score;
  std::uint64_t total_evaluated = 0;
  std::size_t selected_order = 0;
  std::size_t evaluated_orders = 0;
  for (std::size_t order_index = 0; order_index < orders.size(); ++order_index) {
    const std::vector<std::size_t>& order = orders[order_index];
    if (order.size() != prepared.placement.nodes.size()) continue;
    ++evaluated_orders;
    std::vector<bool> placed(prepared.placement.nodes.size(), false);
    std::vector<std::uint64_t> offsets(prepared.placement.nodes.size(), 0);
    bool complete = true;
    for (std::size_t current : order) {
      const std::set<std::uint64_t> candidates =
          detail::CanonicalOffsetsForNode(problem, prepared, placed, offsets, current, true);
      std::optional<std::tuple<std::uint64_t, std::uint64_t>> best;
      for (std::uint64_t offset : candidates) {
        ++total_evaluated;
        if (!detail::FitsReuseNodeAt(problem, prepared, placed, offsets, current, offset)) {
          continue;
        }
        const auto score = std::make_tuple(
            detail::IncrementalReuseCost(prepared, placed, offsets, current, offset), offset);
        if (!best || score < *best) best = score;
      }
      if (!best) {
        complete = false;
        break;
      }
      offsets[current] = std::get<1>(*best);
      placed[current] = true;
    }
    if (!complete) continue;
    DsaResult candidate =
        detail::BuildValidatedReuseResult(problem, prepared, offsets, SolveStatus::kFeasible);
    if (candidate.status != SolveStatus::kFeasible) continue;
    const Score score = ObjectiveScore(problem, candidate.objective);
    if (!result.solution.has_value() || score < best_score) {
      result = std::move(candidate);
      best_score = score;
      selected_order = order_index;
    }
    if (result.objective.reuse_cost == 0) break;
  }
  if (!result.solution.has_value()) {
    result.status = SolveStatus::kBestEffortNoFit;
    result.diagnostics.emplace_back(
        "canonical_greedy found no capacity-fitting canonical placement");
  }
  result.solver_metrics = {
      {"candidate_offsets_evaluated", total_evaluated},
      {"orders_evaluated", evaluated_orders},
      {"selected_order", selected_order},
      {"random_restarts_requested", options_.random_restarts},
      {"seed", options_.seed},
      {"soft_edges", prepared.soft_weights.size()},
  };
  result.diagnostics.emplace_back(
      "canonical_greedy uses the hard-or-soft support menu under size, birth, "
      "soft-weight, and seeded random orders");
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
  const std::vector<std::vector<std::size_t>> node_orders =
      CanonicalGreedyOrders(problem, prepared, CanonicalGreedyOptions{0, 0});
  std::size_t order_index = 0;
  std::uint64_t attempts = 0;
  std::uint64_t demotions = 0;
  DsaResult last;
  while (true) {
    const DsaProblem promoted =
        detail::WithPromotedReuseEdges(problem, prepared, ActivePairs(active));
    std::vector<BufferId> priority;
    if (order_index < node_orders.size()) {
      for (std::size_t node : node_orders[order_index]) {
        priority.push_back(prepared.placement.nodes[node].representative);
      }
    }
    last = detail::PlaceWithOrder(promoted, priority);
    ++attempts;
    if (last.status == SolveStatus::kFeasible && last.solution.has_value()) {
      last.objective = EvaluateObjective(problem, last.solution.value());
      last.solver_metrics = {
          {"initial_soft_edges", prepared.soft_weights.size()},
          {"active_soft_edges", active.size()},
          {"demoted_edges", demotions},
          {"packing_attempts", attempts},
          {"decode_orders_tried", order_index + 1},
      };
      last.diagnostics.emplace_back(
          "promote_repair decoded all active soft edges as separations and repaired "
          "over-capacity support chains");
      return last;
    }
    if (!last.solution || active.empty()) break;
    const std::optional<NodePair> demote =
        CheapestSoftEdgeOnOverflowChain(promoted, prepared, *last.solution, active);
    if (!demote) {
      ++order_index;
      if (order_index < node_orders.size()) {
        continue;
      }
      break;
    }
    active.erase(*demote);
    ++demotions;
  }

  DsaResult fallback = CanonicalGreedySolver().Solve(problem);
  if (fallback.status == SolveStatus::kFeasible) {
    fallback.solver_metrics["initial_soft_edges"] = prepared.soft_weights.size();
    fallback.solver_metrics["demoted_edges"] = demotions;
    fallback.solver_metrics["packing_attempts"] = attempts;
    fallback.solver_metrics["decode_orders_tried"] = std::min(order_index + 1, node_orders.size());
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
      {"decode_orders_tried", std::min(order_index + 1, node_orders.size())},
      {"fallback_to_canonical_greedy", 1},
  };
  last.diagnostics.emplace_back(
      "promote_repair could not repair the decoded placement within capacity");
  return last;
}

PromoteAllSolver::PromoteAllSolver(PromoteAllOptions options) : options_(options) {}

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
  const detail::ReusePenaltySearchSpace promoted_search =
      detail::BuildReusePenaltySearchSpace(promoted);
  detail::CanonicalExactSearchOptions options;
  options.node_limit = options_.max_search_nodes;
  options.stop_after_first = true;
  const detail::CanonicalExactSearchResult exact =
      detail::RunCanonicalExactSearch(promoted, promoted_search, options);
  DsaResult result;
  if (exact.offsets.has_value()) {
    result = detail::BuildValidatedReuseResult(promoted, promoted_search, exact.offsets.value(),
                                               exact.status);
    if (result.solution.has_value()) {
      result.objective = EvaluateObjective(problem, result.solution.value());
    }
  } else {
    result.status = exact.status;
  }
  result.solver_metrics = {
      {"promoted_edges", prepared.soft_weights.size()},
      {"search_nodes", exact.search_nodes},
      {"zero_penalty_feasibility_proven",
       exact.status == SolveStatus::kFeasible || exact.status == SolveStatus::kInfeasibleProven
           ? 1U
           : 0U},
  };
  result.diagnostics.emplace_back(
      exact.status == SolveStatus::kInfeasibleProven
          ? "promote_all proved that no zero-penalty placement fits capacity"
          : "promote_all treats every soft edge as a separation and uses the "
            "canonical feasibility oracle");
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
      if (pool == nullptr || !pool->capacity.has_value() || unit == 0) {
        DsaResult result;
        result.status = SolveStatus::kUnsupported;
        result.diagnostics.emplace_back("unit_random_coloring requires fixed unit capacities");
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
      std::uniform_int_distribution<std::uint64_t> color(0, colors - 1);
      offsets[node] = color(random) * unit;
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
      options_.samples == 1 ? "unit_random_coloring samples independent uniform address colors on "
                              "the geometry-free unit-size slice"
                            : "unit_random_coloring reports the best objective among independent "
                              "uniform-color samples");
  return result;
}

UnitLowRankRoundingSolver::UnitLowRankRoundingSolver(UnitLowRankRoundingOptions options)
    : options_(options) {}

const char* UnitLowRankRoundingSolver::Name() const noexcept { return "unit_low_rank_rounding"; }

SolverCapabilities UnitLowRankRoundingSolver::Capabilities() const noexcept {
  SolverCapabilities capabilities = BaselineCapabilities();
  capabilities.reserved_ranges = false;
  return capabilities;
}

DsaResult UnitLowRankRoundingSolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    return RejectUnsupported(problem, Name(), Capabilities());
  }
  if (options_.rank == 0 || options_.rounding_samples == 0) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics.emplace_back(
        "unit_low_rank_rounding requires positive rank and sample count");
    return result;
  }
  const BaselineProblem prepared = detail::BuildReusePenaltySearchSpace(problem);
  if (!prepared.placement.errors.empty()) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = prepared.placement.errors;
    return result;
  }
  if (prepared.placement.nodes.size() > options_.max_nodes) {
    DsaResult result;
    result.status = SolveStatus::kUnsupported;
    result.diagnostics.emplace_back("unit_low_rank_rounding exceeds max_nodes");
    return result;
  }
  for (const auto& neighbors : prepared.hard_neighbors) {
    if (!neighbors.empty()) {
      DsaResult result;
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.emplace_back("unit_low_rank_rounding requires a geometry-free hard graph");
      return result;
    }
  }

  std::map<PoolId, std::pair<std::uint64_t, std::uint64_t>> pool_shape;
  for (const Pool& pool : problem.pools) {
    if (!pool.capacity.has_value() || !pool.reserved_ranges.empty()) {
      DsaResult result;
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.emplace_back(
          "unit_low_rank_rounding requires fixed unreserved capacities");
      return result;
    }
  }
  for (const detail::PlacementSearchNode& node : prepared.placement.nodes) {
    auto& [unit, colors] = pool_shape[node.pool];
    if (unit == 0) {
      unit = node.size;
      const Pool* pool = problem.FindPool(node.pool);
      colors =
          pool == nullptr || !pool->capacity.has_value() || unit == 0 ? 0 : *pool->capacity / unit;
    }
    if (node.size != unit || unit % node.alignment != 0 || colors == 0) {
      DsaResult result;
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.emplace_back(
          "unit_low_rank_rounding requires uniform unit sizes and compatible "
          "alignment");
      return result;
    }
  }
  std::map<PoolId, std::size_t> node_count_by_pool;
  for (const detail::PlacementSearchNode& node : prepared.placement.nodes) {
    ++node_count_by_pool[node.pool];
  }
  for (auto& [pool, shape] : pool_shape) {
    shape.second = std::min<std::uint64_t>(shape.second, node_count_by_pool.at(pool));
  }

  const std::size_t count = prepared.placement.nodes.size();
  const std::size_t rank = std::min(options_.rank, std::max<std::size_t>(2, count));
  std::mt19937_64 random(options_.seed);
  std::normal_distribution<long double> normal(0.0L, 1.0L);
  std::vector<std::vector<long double>> vectors(count, std::vector<long double>(rank, 0.0L));
  for (auto& vector : vectors) {
    long double norm = 0.0L;
    for (long double& value : vector) {
      value = normal(random);
      norm += value * value;
    }
    norm = std::sqrt(std::max<long double>(norm, 1.0e-18L));
    for (long double& value : vector) value /= norm;
  }

  long double max_weight = 1.0L;
  for (const auto& [pair, weight] : prepared.soft_weights) {
    static_cast<void>(pair);
    max_weight = std::max(max_weight, static_cast<long double>(weight));
  }
  std::vector<std::vector<long double>> gradient(count, std::vector<long double>(rank, 0.0L));
  for (std::size_t iteration = 0; iteration < options_.relaxation_iterations; ++iteration) {
    for (auto& row : gradient) std::fill(row.begin(), row.end(), 0.0L);
    for (const auto& [pair, weight] : prepared.soft_weights) {
      const auto colors = pool_shape.at(prepared.placement.nodes[pair.first].pool).second;
      const long double lower = colors <= 1 ? 1.0L : -1.0L / static_cast<long double>(colors - 1);
      long double dot = 0.0L;
      for (std::size_t dimension = 0; dimension < rank; ++dimension) {
        dot += vectors[pair.first][dimension] * vectors[pair.second][dimension];
      }
      const long double scaled = static_cast<long double>(weight) / max_weight;
      const long double repair = dot < lower ? 4.0L * (lower - dot) : 0.0L;
      for (std::size_t dimension = 0; dimension < rank; ++dimension) {
        gradient[pair.first][dimension] +=
            -scaled * vectors[pair.second][dimension] + repair * vectors[pair.second][dimension];
        gradient[pair.second][dimension] +=
            -scaled * vectors[pair.first][dimension] + repair * vectors[pair.first][dimension];
      }
    }
    const long double step = 0.1L / std::sqrt(static_cast<long double>(iteration + 1));
    for (std::size_t node = 0; node < count; ++node) {
      long double norm = 0.0L;
      for (std::size_t dimension = 0; dimension < rank; ++dimension) {
        vectors[node][dimension] += step * gradient[node][dimension];
        norm += vectors[node][dimension] * vectors[node][dimension];
      }
      norm = std::sqrt(std::max<long double>(norm, 1.0e-18L));
      for (long double& value : vectors[node]) value /= norm;
    }
  }

  std::optional<DsaResult> best;
  Score best_score;
  for (std::size_t sample = 0; sample < options_.rounding_samples; ++sample) {
    std::map<PoolId, std::vector<std::vector<long double>>> directions;
    for (const auto& [pool, shape] : pool_shape) {
      auto& pool_directions = directions[pool];
      pool_directions.assign(static_cast<std::size_t>(shape.second),
                             std::vector<long double>(rank, 0.0L));
      for (auto& direction : pool_directions) {
        for (long double& value : direction) value = normal(random);
      }
    }
    std::vector<std::uint64_t> offsets(count, 0);
    for (std::size_t node = 0; node < count; ++node) {
      const auto [unit, colors] = pool_shape.at(prepared.placement.nodes[node].pool);
      std::uint64_t best_color = 0;
      long double best_projection = -std::numeric_limits<long double>::infinity();
      for (std::uint64_t color = 0; color < colors; ++color) {
        long double projection = 0.0L;
        for (std::size_t dimension = 0; dimension < rank; ++dimension) {
          projection +=
              vectors[node][dimension] *
              directions.at(
                  prepared.placement.nodes[node].pool)[static_cast<std::size_t>(color)][dimension];
        }
        if (projection > best_projection) {
          best_projection = projection;
          best_color = color;
        }
      }
      offsets[node] = best_color * unit;
    }
    DsaResult candidate =
        detail::BuildValidatedReuseResult(problem, prepared, offsets, SolveStatus::kFeasible);
    if (candidate.status != SolveStatus::kFeasible) return candidate;
    const Score score = ObjectiveScore(problem, candidate.objective);
    if (!best.has_value() || score < best_score) {
      best = std::move(candidate);
      best_score = score;
    }
  }
  DsaResult result = std::move(best).value();
  result.solver_metrics = {
      {"relaxation_iterations", options_.relaxation_iterations},
      {"rounding_samples", options_.rounding_samples},
      {"rank", rank},
      {"seed", options_.seed},
  };
  result.diagnostics.emplace_back(
      "unit_low_rank_rounding uses a nonconvex numerical vector heuristic and "
      "Gaussian argmax rounding; it is not an SDP solver and carries no "
      "Frieze-Jerrum guarantee");
  return result;
}

}  // namespace dsa

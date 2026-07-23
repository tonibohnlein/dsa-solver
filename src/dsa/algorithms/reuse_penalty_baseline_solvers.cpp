// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/reuse_penalty_baseline_solvers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dsa/algorithms/placement_engine.h"
#include "dsa/algorithms/solver.h"
#include "dsa/model/model.h"
#include "dsa/model/validator.h"

namespace dsa {
namespace {

using NodePair = std::pair<std::size_t, std::size_t>;

struct BaselineProblem {
  detail::PlacementSearchSpace search;
  std::unordered_map<BufferId, std::size_t> node_by_buffer;
  std::vector<std::set<std::size_t>> hard_neighbors;
  std::map<NodePair, std::uint64_t> soft_weights;
  std::vector<std::vector<std::pair<std::size_t, std::uint64_t>>> soft_neighbors;
};

bool AddOverflows(std::uint64_t first, std::uint64_t second) {
  return first > std::numeric_limits<std::uint64_t>::max() - second;
}

std::uint64_t SaturatingAdd(std::uint64_t first, std::uint64_t second) {
  return AddOverflows(first, second) ? std::numeric_limits<std::uint64_t>::max() : first + second;
}

std::optional<std::uint64_t> AlignUp(std::uint64_t value, std::uint64_t alignment) {
  if (alignment <= 1) return value;
  const std::uint64_t remainder = value % alignment;
  if (remainder == 0) return value;
  const std::uint64_t delta = alignment - remainder;
  if (AddOverflows(value, delta)) return std::nullopt;
  return value + delta;
}

bool Overlap(std::uint64_t first_offset, std::uint64_t first_size, std::uint64_t second_offset,
             std::uint64_t second_size) {
  return AddressRangesOverlap(first_offset, first_size, second_offset, second_size);
}

NodePair CanonicalPair(std::size_t first, std::size_t second) {
  return first < second ? NodePair{first, second} : NodePair{second, first};
}

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

std::optional<BaselineProblem> PrepareBaseline(const DsaProblem& problem, DsaResult* error) {
  BaselineProblem prepared;
  prepared.search = detail::BuildPlacementSearchSpace(problem);
  if (!prepared.search.errors.empty()) {
    error->status = SolveStatus::kInvalidProblem;
    error->diagnostics = prepared.search.errors;
    return std::nullopt;
  }
  if (prepared.search.flexible_pools) {
    error->status = SolveStatus::kUnsupported;
    error->diagnostics.emplace_back("DSA-RP baselines do not support flexible pool assignment");
    return std::nullopt;
  }

  const std::size_t count = prepared.search.nodes.size();
  prepared.hard_neighbors.resize(count);
  prepared.soft_neighbors.resize(count);
  std::unordered_map<BufferId, std::size_t> node_by_representative;
  for (std::size_t index = 0; index < count; ++index) {
    const detail::PlacementSearchNode& node = prepared.search.nodes[index];
    node_by_representative.emplace(node.representative, index);
    for (BufferId member : node.members) prepared.node_by_buffer.emplace(member, index);
  }
  for (std::size_t index = 0; index < count; ++index) {
    for (BufferId neighbor : prepared.search.nodes[index].conflicts) {
      const auto found = node_by_representative.find(neighbor);
      if (found != node_by_representative.end()) {
        prepared.hard_neighbors[index].insert(found->second);
      }
    }
  }

  if (problem.cost_model) {
    for (const ReusePenalty& penalty : problem.cost_model->reuse_penalties) {
      const auto first = prepared.node_by_buffer.find(penalty.first);
      const auto second = prepared.node_by_buffer.find(penalty.second);
      if (first == prepared.node_by_buffer.end() || second == prepared.node_by_buffer.end() ||
          first->second == second->second) {
        continue;
      }
      const std::size_t first_node = first->second;
      const std::size_t second_node = second->second;
      if (prepared.search.nodes[first_node].pool != prepared.search.nodes[second_node].pool ||
          prepared.hard_neighbors[first_node].count(second_node) != 0) {
        continue;
      }
      const NodePair pair = CanonicalPair(first_node, second_node);
      prepared.soft_weights[pair] = SaturatingAdd(prepared.soft_weights[pair], penalty.cost);
    }
  }
  for (const auto& [pair, weight] : prepared.soft_weights) {
    prepared.soft_neighbors[pair.first].emplace_back(pair.second, weight);
    prepared.soft_neighbors[pair.second].emplace_back(pair.first, weight);
  }
  return prepared;
}

std::vector<std::size_t> DefaultNodeOrder(const DsaProblem& problem,
                                          const BaselineProblem& prepared) {
  const std::vector<BufferId> representatives = detail::DefaultPlacementOrder(problem);
  std::unordered_map<BufferId, std::size_t> node_by_representative;
  for (std::size_t index = 0; index < prepared.search.nodes.size(); ++index) {
    node_by_representative.emplace(prepared.search.nodes[index].representative, index);
  }
  std::vector<std::size_t> order;
  order.reserve(prepared.search.nodes.size());
  for (BufferId representative : representatives) {
    const auto found = node_by_representative.find(representative);
    if (found != node_by_representative.end()) order.push_back(found->second);
  }
  return order;
}

bool FitsAt(const DsaProblem& problem, const BaselineProblem& prepared,
            const std::vector<bool>& placed, const std::vector<std::uint64_t>& offsets,
            std::size_t current, std::uint64_t offset) {
  const detail::PlacementSearchNode& node = prepared.search.nodes[current];
  if (AddOverflows(offset, node.size)) return false;
  const Pool* pool = problem.FindPool(node.pool);
  if (pool == nullptr || (pool->capacity && offset + node.size > *pool->capacity)) return false;
  for (const AddressRange& reserved : pool->reserved_ranges) {
    if (Overlap(offset, node.size, reserved.begin, reserved.end - reserved.begin)) return false;
  }
  for (std::size_t other : prepared.hard_neighbors[current]) {
    if (!placed[other]) continue;
    const detail::PlacementSearchNode& other_node = prepared.search.nodes[other];
    if (other_node.pool == node.pool &&
        Overlap(offset, node.size, offsets[other], other_node.size)) {
      return false;
    }
  }
  return true;
}

std::uint64_t IncrementalPenalty(const BaselineProblem& prepared, const std::vector<bool>& placed,
                                 const std::vector<std::uint64_t>& offsets, std::size_t current,
                                 std::uint64_t offset) {
  const detail::PlacementSearchNode& node = prepared.search.nodes[current];
  std::uint64_t cost = 0;
  for (const auto& [other, weight] : prepared.soft_neighbors[current]) {
    if (!placed[other]) continue;
    const detail::PlacementSearchNode& other_node = prepared.search.nodes[other];
    if (Overlap(offset, node.size, offsets[other], other_node.size)) {
      cost = SaturatingAdd(cost, weight);
    }
  }
  return cost;
}

DsaResult BuildResult(const DsaProblem& problem, const BaselineProblem& prepared,
                      const std::vector<std::uint64_t>& offsets, SolveStatus status) {
  DsaResult result;
  DsaSolution solution;
  for (std::size_t index = 0; index < prepared.search.nodes.size(); ++index) {
    const detail::PlacementSearchNode& node = prepared.search.nodes[index];
    for (BufferId member : node.members) {
      solution.placements.emplace(member, Placement{node.pool, offsets[index]});
    }
  }
  const std::vector<std::string> errors = ValidateSolution(problem, solution);
  if (!errors.empty()) {
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics.emplace_back("DSA-RP baseline produced an invalid placement");
    result.diagnostics.insert(result.diagnostics.end(), errors.begin(), errors.end());
    return result;
  }
  result.status = status;
  result.objective = EvaluateObjective(problem, solution);
  result.solution = std::move(solution);
  return result;
}

std::map<NodePair, std::uint64_t> ActiveSoftEdges(const BaselineProblem& prepared) {
  return prepared.soft_weights;
}

DsaProblem WithPromotedEdges(const DsaProblem& problem, const BaselineProblem& prepared,
                             const std::map<NodePair, std::uint64_t>& active) {
  DsaProblem promoted = problem;
  for (const auto& [pair, weight] : active) {
    static_cast<void>(weight);
    promoted.separations.push_back({prepared.search.nodes[pair.first].representative,
                                    prepared.search.nodes[pair.second].representative,
                                    {SeparationReason::kGeneric}});
  }
  return promoted;
}

std::optional<NodePair> CheapestSoftEdgeOnOverflowChain(
    const DsaProblem& promoted_problem, const BaselineProblem& prepared,
    const DsaSolution& solution, const std::map<NodePair, std::uint64_t>& active) {
  const detail::PlacementSearchSpace promoted_search =
      detail::BuildPlacementSearchSpace(promoted_problem);
  if (!promoted_search.errors.empty() ||
      promoted_search.nodes.size() != prepared.search.nodes.size()) {
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
        AddOverflows(placement->offset, node.size)) {
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
          AddOverflows(neighbor_placement->offset, neighbor_node.size) ||
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
    const NodePair pair = CanonicalPair(current_prepared->second, support_prepared->second);
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
  const std::optional<BaselineProblem> maybe_prepared = PrepareBaseline(problem, &error);
  if (!maybe_prepared) return error;
  const BaselineProblem& prepared = *maybe_prepared;
  const std::vector<std::size_t> order = DefaultNodeOrder(problem, prepared);
  if (order.size() != prepared.search.nodes.size()) {
    error.status = SolveStatus::kInvalidProblem;
    error.diagnostics.emplace_back("canonical_greedy could not construct a complete buffer order");
    return error;
  }

  std::vector<bool> placed(prepared.search.nodes.size(), false);
  std::vector<std::uint64_t> offsets(prepared.search.nodes.size(), 0);
  std::uint64_t evaluated = 0;
  for (std::size_t current : order) {
    const detail::PlacementSearchNode& node = prepared.search.nodes[current];
    std::set<std::uint64_t> candidates{0};
    const Pool* pool = problem.FindPool(node.pool);
    for (const AddressRange& reserved : pool->reserved_ranges) candidates.insert(reserved.end);
    for (std::size_t other : prepared.hard_neighbors[current]) {
      if (placed[other] && !AddOverflows(offsets[other], prepared.search.nodes[other].size)) {
        candidates.insert(offsets[other] + prepared.search.nodes[other].size);
      }
    }
    for (const auto& [other, weight] : prepared.soft_neighbors[current]) {
      static_cast<void>(weight);
      if (placed[other] && !AddOverflows(offsets[other], prepared.search.nodes[other].size)) {
        candidates.insert(offsets[other] + prepared.search.nodes[other].size);
      }
    }

    std::optional<std::tuple<std::uint64_t, std::uint64_t>> best;
    for (std::uint64_t raw : candidates) {
      const std::optional<std::uint64_t> offset = AlignUp(raw, node.alignment);
      if (!offset) continue;
      ++evaluated;
      if (!FitsAt(problem, prepared, placed, offsets, current, *offset)) continue;
      const auto score =
          std::make_tuple(IncrementalPenalty(prepared, placed, offsets, current, *offset), *offset);
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

  DsaResult result = BuildResult(problem, prepared, offsets, SolveStatus::kFeasible);
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
  const std::optional<BaselineProblem> maybe_prepared = PrepareBaseline(problem, &error);
  if (!maybe_prepared) return error;
  const BaselineProblem& prepared = *maybe_prepared;
  std::map<NodePair, std::uint64_t> active = ActiveSoftEdges(prepared);
  std::uint64_t attempts = 0;
  std::uint64_t demotions = 0;
  DsaResult last;
  while (true) {
    const DsaProblem promoted = WithPromotedEdges(problem, prepared, active);
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

}  // namespace dsa

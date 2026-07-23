// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/reuse_penalty_search.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dsa/algorithms/placement_engine.h"
#include "dsa/model/model.h"
#include "dsa/model/validator.h"

namespace dsa::detail {

bool AddOverflows(std::uint64_t first, std::uint64_t second) noexcept {
  return first > std::numeric_limits<std::uint64_t>::max() - second;
}

std::uint64_t SaturatingAdd(std::uint64_t first, std::uint64_t second) noexcept {
  return AddOverflows(first, second) ? std::numeric_limits<std::uint64_t>::max() : first + second;
}

std::optional<std::uint64_t> AlignUp(std::uint64_t value, std::uint64_t alignment) noexcept {
  if (alignment <= 1) {
    return value;
  }
  const std::uint64_t remainder = value % alignment;
  if (remainder == 0) {
    return value;
  }
  const std::uint64_t delta = alignment - remainder;
  if (AddOverflows(value, delta)) {
    return std::nullopt;
  }
  return value + delta;
}

ReuseNodePair CanonicalNodePair(std::size_t first, std::size_t second) noexcept {
  return first < second ? ReuseNodePair{first, second} : ReuseNodePair{second, first};
}

ReusePenaltySearchSpace BuildReusePenaltySearchSpace(const DsaProblem& problem) {
  ReusePenaltySearchSpace search;
  search.placement = BuildPlacementSearchSpace(problem);
  if (!search.placement.errors.empty() || search.placement.flexible_pools) {
    return search;
  }

  const std::size_t count = search.placement.nodes.size();
  search.hard_neighbors.resize(count);
  search.soft_neighbors.resize(count);
  std::unordered_map<BufferId, std::size_t> node_by_representative;
  for (std::size_t index = 0; index < count; ++index) {
    const PlacementSearchNode& node = search.placement.nodes[index];
    node_by_representative.emplace(node.representative, index);
    for (BufferId member : node.members) {
      search.node_by_buffer.emplace(member, index);
    }
  }
  for (std::size_t index = 0; index < count; ++index) {
    for (BufferId neighbor : search.placement.nodes[index].conflicts) {
      const auto found = node_by_representative.find(neighbor);
      if (found != node_by_representative.end()) {
        search.hard_neighbors[index].insert(found->second);
      }
    }
  }

  if (problem.cost_model.has_value()) {
    for (const ReusePenalty& penalty : problem.cost_model->reuse_penalties) {
      const auto first = search.node_by_buffer.find(penalty.first);
      const auto second = search.node_by_buffer.find(penalty.second);
      if (first == search.node_by_buffer.end() || second == search.node_by_buffer.end() ||
          first->second == second->second) {
        continue;
      }
      const std::size_t first_node = first->second;
      const std::size_t second_node = second->second;
      if (search.placement.nodes[first_node].pool != search.placement.nodes[second_node].pool ||
          search.hard_neighbors[first_node].count(second_node) != 0) {
        continue;
      }
      const ReuseNodePair pair = CanonicalNodePair(first_node, second_node);
      search.soft_weights[pair] = SaturatingAdd(search.soft_weights[pair], penalty.cost);
    }
  }
  for (const auto& [pair, weight] : search.soft_weights) {
    search.soft_neighbors[pair.first].emplace_back(pair.second, weight);
    search.soft_neighbors[pair.second].emplace_back(pair.first, weight);
  }
  return search;
}

std::vector<std::size_t> DefaultReuseNodeOrder(const DsaProblem& problem,
                                               const ReusePenaltySearchSpace& search) {
  const std::vector<BufferId> representatives = DefaultPlacementOrder(problem);
  std::unordered_map<BufferId, std::size_t> node_by_representative;
  for (std::size_t index = 0; index < search.placement.nodes.size(); ++index) {
    node_by_representative.emplace(search.placement.nodes[index].representative, index);
  }
  std::vector<std::size_t> order;
  order.reserve(search.placement.nodes.size());
  for (BufferId representative : representatives) {
    const auto found = node_by_representative.find(representative);
    if (found != node_by_representative.end()) {
      order.push_back(found->second);
    }
  }
  return order;
}

bool FitsReuseNodeAt(const DsaProblem& problem, const ReusePenaltySearchSpace& search,
                     const std::vector<bool>& placed, const std::vector<std::uint64_t>& offsets,
                     std::size_t current, std::uint64_t offset) {
  const PlacementSearchNode& node = search.placement.nodes[current];
  if (AddOverflows(offset, node.size)) {
    return false;
  }
  const Pool* pool = problem.FindPool(node.pool);
  if (pool == nullptr ||
      (pool->capacity.has_value() && offset + node.size > pool->capacity.value())) {
    return false;
  }
  for (const AddressRange& reserved : pool->reserved_ranges) {
    if (AddressRangesOverlap(offset, node.size, reserved.begin, reserved.end - reserved.begin)) {
      return false;
    }
  }
  for (std::size_t other : search.hard_neighbors[current]) {
    if (!placed[other]) {
      continue;
    }
    const PlacementSearchNode& other_node = search.placement.nodes[other];
    if (other_node.pool == node.pool &&
        AddressRangesOverlap(offset, node.size, offsets[other], other_node.size)) {
      return false;
    }
  }
  return true;
}

std::uint64_t IncrementalReuseCost(const ReusePenaltySearchSpace& search,
                                   const std::vector<bool>& placed,
                                   const std::vector<std::uint64_t>& offsets, std::size_t current,
                                   std::uint64_t offset) {
  const PlacementSearchNode& node = search.placement.nodes[current];
  std::uint64_t cost = 0;
  for (const auto& [other, weight] : search.soft_neighbors[current]) {
    if (!placed[other]) {
      continue;
    }
    const PlacementSearchNode& other_node = search.placement.nodes[other];
    if (AddressRangesOverlap(offset, node.size, offsets[other], other_node.size)) {
      cost = SaturatingAdd(cost, weight);
    }
  }
  return cost;
}

std::set<std::uint64_t> CanonicalOffsetsForNode(const DsaProblem& problem,
                                                const ReusePenaltySearchSpace& search,
                                                const std::vector<bool>& placed,
                                                const std::vector<std::uint64_t>& offsets,
                                                std::size_t current, bool include_soft_supports) {
  const PlacementSearchNode& node = search.placement.nodes[current];
  std::set<std::uint64_t> candidates{0};
  const Pool* pool = problem.FindPool(node.pool);
  if (pool != nullptr) {
    for (const AddressRange& reserved : pool->reserved_ranges) {
      candidates.insert(reserved.end);
    }
  }
  for (std::size_t other : search.hard_neighbors[current]) {
    if (placed[other] && !AddOverflows(offsets[other], search.placement.nodes[other].size)) {
      candidates.insert(offsets[other] + search.placement.nodes[other].size);
    }
  }
  if (include_soft_supports) {
    for (const auto& [other, weight] : search.soft_neighbors[current]) {
      static_cast<void>(weight);
      if (placed[other] && !AddOverflows(offsets[other], search.placement.nodes[other].size)) {
        candidates.insert(offsets[other] + search.placement.nodes[other].size);
      }
    }
  }

  std::set<std::uint64_t> aligned;
  for (std::uint64_t candidate : candidates) {
    const std::optional<std::uint64_t> offset = AlignUp(candidate, node.alignment);
    if (offset.has_value()) {
      aligned.insert(offset.value());
    }
  }
  return aligned;
}

DsaSolution ExpandReuseNodeSolution(const ReusePenaltySearchSpace& search,
                                    const std::vector<std::uint64_t>& offsets) {
  DsaSolution solution;
  for (std::size_t index = 0; index < search.placement.nodes.size(); ++index) {
    const PlacementSearchNode& node = search.placement.nodes[index];
    for (BufferId member : node.members) {
      solution.placements.emplace(member, Placement{node.pool, offsets[index]});
    }
  }
  return solution;
}

DsaResult BuildValidatedReuseResult(const DsaProblem& problem,
                                    const ReusePenaltySearchSpace& search,
                                    const std::vector<std::uint64_t>& offsets, SolveStatus status) {
  DsaResult result;
  DsaSolution solution = ExpandReuseNodeSolution(search, offsets);
  const std::vector<std::string> errors = ValidateSolution(problem, solution);
  if (!errors.empty()) {
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics.emplace_back("DSA-RP algorithm produced an invalid placement");
    result.diagnostics.insert(result.diagnostics.end(), errors.begin(), errors.end());
    return result;
  }
  result.status = status;
  result.objective = EvaluateObjective(problem, solution);
  result.solution = std::move(solution);
  return result;
}

DsaProblem WithPromotedReuseEdges(const DsaProblem& problem, const ReusePenaltySearchSpace& search,
                                  const std::set<ReuseNodePair>& promoted) {
  DsaProblem result = problem;
  for (const ReuseNodePair& pair : promoted) {
    result.separations.emplace_back(search.placement.nodes[pair.first].representative,
                                    search.placement.nodes[pair.second].representative,
                                    std::vector<SeparationReason>{SeparationReason::kGeneric});
  }
  return result;
}

}  // namespace dsa::detail

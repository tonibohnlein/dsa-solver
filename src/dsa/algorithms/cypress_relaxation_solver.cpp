// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/cypress_relaxation_solver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dsa/algorithms/placement_engine.h"
#include "dsa/model/validator.h"

namespace dsa {
namespace {

struct AuxiliaryEdge {
  std::size_t first = 0;
  std::size_t second = 0;
};

struct PoolPackResult {
  std::vector<std::uint64_t> offsets;
  std::uint64_t peak = 0;
  bool within_capacity = false;
};

bool AddOverflows(std::uint64_t first, std::uint64_t second) {
  return first > std::numeric_limits<std::uint64_t>::max() - second;
}

std::optional<std::uint64_t> AlignUp(std::uint64_t value, std::uint64_t alignment) {
  const std::uint64_t remainder = value % alignment;
  if (remainder == 0) return value;
  const std::uint64_t increment = alignment - remainder;
  if (AddOverflows(value, increment)) return std::nullopt;
  return value + increment;
}

bool RangesOverlap(std::uint64_t first_offset, std::uint64_t first_size,
                   std::uint64_t second_offset, std::uint64_t second_size) {
  if (AddOverflows(first_offset, first_size) || AddOverflows(second_offset, second_size)) {
    return true;
  }
  return first_offset < second_offset + second_size && second_offset < first_offset + first_size;
}

std::optional<std::uint64_t> AvoidReservedRanges(const Pool& pool, std::uint64_t offset,
                                                 std::uint64_t size, std::uint64_t alignment) {
  for (;;) {
    bool moved = false;
    for (const AddressRange& reserved : pool.reserved_ranges) {
      if (!RangesOverlap(offset, size, reserved.begin, reserved.end - reserved.begin)) continue;
      const std::optional<std::uint64_t> next = AlignUp(reserved.end, alignment);
      if (!next) return std::nullopt;
      offset = *next;
      moved = true;
      break;
    }
    if (!moved) return offset;
  }
}

PoolPackResult PackPool(const Pool& pool, const std::vector<std::size_t>& pool_nodes,
                        const std::vector<detail::PlacementSearchNode>& nodes,
                        const std::vector<std::vector<bool>>& interference) {
  PoolPackResult result;
  result.offsets.assign(nodes.size(), 0);
  std::vector<bool> finalized(nodes.size(), false);
  std::vector<std::size_t> interference_degree(nodes.size(), 0);
  for (std::size_t node : pool_nodes) {
    const std::optional<std::uint64_t> offset =
        AvoidReservedRanges(pool, 0, nodes[node].size, nodes[node].alignment);
    result.offsets[node] = offset.value_or(std::numeric_limits<std::uint64_t>::max());
    interference_degree[node] = static_cast<std::size_t>(std::count_if(
        pool_nodes.begin(), pool_nodes.end(),
        [&](std::size_t other) { return other != node && interference[node][other]; }));
  }

  for (std::size_t count = 0; count < pool_nodes.size(); ++count) {
    const auto selected = std::min_element(
        pool_nodes.begin(), pool_nodes.end(), [&](std::size_t first, std::size_t second) {
          if (finalized[first] != finalized[second]) return !finalized[first];
          if (finalized[first]) return first < second;
          const std::size_t first_degree = interference_degree[first];
          const std::size_t second_degree = interference_degree[second];
          if (result.offsets[first] != result.offsets[second]) {
            return result.offsets[first] < result.offsets[second];
          }
          if (first_degree != second_degree) return first_degree > second_degree;
          if (nodes[first].size != nodes[second].size) {
            return nodes[first].size > nodes[second].size;
          }
          return nodes[first].representative < nodes[second].representative;
        });
    if (selected == pool_nodes.end() || finalized[*selected]) break;
    const std::size_t current = *selected;
    finalized[current] = true;
    if (AddOverflows(result.offsets[current], nodes[current].size)) {
      result.peak = std::numeric_limits<std::uint64_t>::max();
      result.within_capacity = false;
      return result;
    }
    result.peak = std::max(result.peak, result.offsets[current] + nodes[current].size);

    for (std::size_t other : pool_nodes) {
      if (finalized[other] || !interference[current][other]) continue;
      if (!RangesOverlap(result.offsets[current], nodes[current].size, result.offsets[other],
                         nodes[other].size)) {
        continue;
      }
      const std::optional<std::uint64_t> shifted =
          AlignUp(result.offsets[current] + nodes[current].size, nodes[other].alignment);
      if (!shifted) {
        result.offsets[other] = std::numeric_limits<std::uint64_t>::max();
        continue;
      }
      const std::optional<std::uint64_t> legal =
          AvoidReservedRanges(pool, *shifted, nodes[other].size, nodes[other].alignment);
      result.offsets[other] = legal.value_or(std::numeric_limits<std::uint64_t>::max());
    }
  }

  result.peak = 0;
  for (std::size_t node : pool_nodes) {
    if (AddOverflows(result.offsets[node], nodes[node].size)) {
      result.peak = std::numeric_limits<std::uint64_t>::max();
      break;
    }
    result.peak = std::max(result.peak, result.offsets[node] + nodes[node].size);
  }
  result.within_capacity = pool.capacity && result.peak <= *pool.capacity;
  return result;
}

}  // namespace

const char* CypressRelaxationSolver::Name() const noexcept { return "cypress_relaxation"; }

SolverCapabilities CypressRelaxationSolver::Capabilities() const noexcept {
  SolverCapabilities capabilities;
  capabilities.colocations = true;
  capabilities.separations = true;
  capabilities.reserved_ranges = true;
  capabilities.multi_pool = true;
  capabilities.lexicographic_objective = true;
  capabilities.capacity_objective = true;
  return capabilities;
}

DsaResult CypressRelaxationSolver::Solve(const DsaProblem& problem) const {
  DsaResult result;
  const std::vector<std::string> validation = ValidateProblem(problem);
  if (!validation.empty()) {
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = validation;
    return result;
  }
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.StructurallyCompatible()) {
    result.status = SolveStatus::kUnsupported;
    for (const std::string& feature : compatibility.unsupported_features) {
      result.diagnostics.push_back("cypress_relaxation does not support feature '" + feature + "'");
    }
    return result;
  }
  for (const Pool& pool : problem.pools) {
    if (!pool.capacity) {
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.push_back(
          "cypress_relaxation requires a fixed capacity for every independent pool");
      return result;
    }
    if (pool.bank_geometry) {
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.push_back("cypress_relaxation does not support bank geometry");
      return result;
    }
  }

  const detail::PlacementSearchSpace search_space = detail::BuildPlacementSearchSpace(problem);
  if (!search_space.errors.empty()) {
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = search_space.errors;
    return result;
  }
  if (search_space.flexible_pools) {
    result.status = SolveStatus::kUnsupported;
    result.diagnostics.push_back("cypress_relaxation does not support flexible pool assignment");
    return result;
  }

  const std::size_t node_count = search_space.nodes.size();
  std::vector<std::vector<bool>> mandatory(node_count, std::vector<bool>(node_count, false));
  std::unordered_map<BufferId, std::size_t> node_by_representative;
  for (std::size_t index = 0; index < node_count; ++index) {
    node_by_representative.emplace(search_space.nodes[index].representative, index);
  }
  for (std::size_t index = 0; index < node_count; ++index) {
    for (BufferId neighbor : search_space.nodes[index].conflicts) {
      const auto found = node_by_representative.find(neighbor);
      if (found == node_by_representative.end()) continue;
      const std::size_t other = found->second;
      mandatory[index][other] = true;
      mandatory[other][index] = true;
    }
  }

  DsaSolution solution;
  std::uint64_t total_auxiliary = 0;
  std::uint64_t total_relaxed = 0;
  std::uint64_t total_actual_aliases = 0;
  std::uint64_t total_attempts = 0;
  bool all_fit = true;

  std::vector<const Pool*> ordered_pools;
  ordered_pools.reserve(problem.pools.size());
  for (const Pool& pool : problem.pools) ordered_pools.push_back(&pool);
  std::sort(ordered_pools.begin(), ordered_pools.end(),
            [](const Pool* first, const Pool* second) { return first->id < second->id; });

  for (const Pool* pool_pointer : ordered_pools) {
    const Pool& pool = *pool_pointer;
    std::vector<std::size_t> pool_nodes;
    for (std::size_t index = 0; index < node_count; ++index) {
      if (search_space.nodes[index].pool == pool.id) pool_nodes.push_back(index);
    }
    std::sort(pool_nodes.begin(), pool_nodes.end(), [&](std::size_t first, std::size_t second) {
      return search_space.nodes[first].representative < search_space.nodes[second].representative;
    });

    std::vector<std::vector<bool>> active = mandatory;
    std::vector<AuxiliaryEdge> auxiliary;
    for (std::size_t first_position = 0; first_position < pool_nodes.size(); ++first_position) {
      for (std::size_t second_position = first_position + 1; second_position < pool_nodes.size();
           ++second_position) {
        const std::size_t first = pool_nodes[first_position];
        const std::size_t second = pool_nodes[second_position];
        if (mandatory[first][second]) continue;
        active[first][second] = true;
        active[second][first] = true;
        auxiliary.push_back({first, second});
      }
    }
    total_auxiliary += auxiliary.size();

    PoolPackResult packed = PackPool(pool, pool_nodes, search_space.nodes, active);
    ++total_attempts;
    std::size_t relaxed = 0;
    while (!packed.within_capacity && relaxed < auxiliary.size()) {
      const AuxiliaryEdge edge = auxiliary[relaxed];
      active[edge.first][edge.second] = false;
      active[edge.second][edge.first] = false;
      ++relaxed;
      packed = PackPool(pool, pool_nodes, search_space.nodes, active);
      ++total_attempts;
    }
    total_relaxed += relaxed;
    all_fit = all_fit && packed.within_capacity;

    for (std::size_t edge_index = 0; edge_index < relaxed; ++edge_index) {
      const AuxiliaryEdge edge = auxiliary[edge_index];
      if (RangesOverlap(packed.offsets[edge.first], search_space.nodes[edge.first].size,
                        packed.offsets[edge.second], search_space.nodes[edge.second].size)) {
        ++total_actual_aliases;
      }
    }
    for (std::size_t index : pool_nodes) {
      for (BufferId member : search_space.nodes[index].members) {
        solution.placements.emplace(member, Placement{pool.id, packed.offsets[index]});
      }
    }
  }

  DsaProblem without_capacity = problem;
  for (Pool& pool : without_capacity.pools) pool.capacity.reset();
  const std::vector<std::string> solution_errors = ValidateSolution(without_capacity, solution);
  if (!solution_errors.empty()) {
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics.push_back("cypress_relaxation internal placement validation failed");
    result.diagnostics.insert(result.diagnostics.end(), solution_errors.begin(),
                              solution_errors.end());
    return result;
  }

  result.solution = std::move(solution);
  result.objective = EvaluateObjective(problem, *result.solution);
  result.status = all_fit ? SolveStatus::kFeasible : SolveStatus::kBestEffortNoFit;
  result.diagnostics.push_back(
      "cypress_relaxation independently implements the PLDI 2025 complete-graph relaxation "
      "policy with the Knight et al. packing heuristic");
  result.diagnostics.push_back(
      "cypress_relaxation deletion_order=stable_pool_then_buffer_id "
      "(the Cypress paper does not specify auxiliary-edge order)");
  result.solver_metrics = {
      {"auxiliary_edges", total_auxiliary},
      {"relaxed_edges", total_relaxed},
      {"actual_alias_pairs", total_actual_aliases},
      {"packing_attempts", total_attempts},
  };
  if (problem.cost_model && !problem.cost_model->reuse_penalties.empty()) {
    result.diagnostics.push_back(
        "cypress_relaxation is an unweighted alias-relaxation baseline; input reuse weights "
        "do not affect its edge-deletion order");
  }
  for (const std::string& objective : compatibility.unsupported_objectives) {
    result.diagnostics.push_back(
        "cypress_relaxation provides a structural capacity baseline but does not optimize '" +
        objective + "'");
  }
  return result;
}

}  // namespace dsa

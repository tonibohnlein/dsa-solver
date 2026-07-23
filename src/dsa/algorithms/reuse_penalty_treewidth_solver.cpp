// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/reuse_penalty_treewidth_solver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "dsa/algorithms/placement_engine.h"
#include "dsa/algorithms/reuse_penalty_search.h"
#include "dsa/algorithms/solver.h"
#include "dsa/model/model.h"
#include "dsa/model/validator.h"

namespace dsa {
namespace {

struct TableValue {
  std::uint64_t cost = 0;
  bool feasible = true;
};

using Partition = std::vector<std::uint32_t>;

struct Factor {
  std::vector<std::size_t> scope;
  std::map<Partition, TableValue> values;
};

struct Trace {
  std::size_t variable = 0;
  std::vector<std::size_t> remaining_scope;
  std::map<Partition, std::uint32_t> best_block;
};

struct PairPotential {
  bool hard = false;
  std::uint64_t soft_weight = 0;
};

struct EliminationOrder {
  std::vector<std::size_t> variables;
  std::size_t width = 0;
};

struct PoolDpResult {
  SolveStatus status = SolveStatus::kUnsupported;
  std::string diagnostic;
  std::vector<std::uint64_t> offsets;
  std::size_t width = 0;
  std::uint64_t states = 0;
  std::uint64_t transitions = 0;
  std::uint64_t reuse_cost = 0;
};

SolverCapabilities TreewidthCapabilities() {
  SolverCapabilities capabilities;
  capabilities.multi_interval = true;
  capabilities.reuse_cost = true;
  capabilities.colocations = true;
  capabilities.separations = true;
  capabilities.temporal_exclusions = true;
  capabilities.multi_pool = true;
  capabilities.lexicographic_objective = true;
  capabilities.capacity_objective = true;
  capabilities.peak_objective = false;
  return capabilities;
}

std::optional<std::string> ObjectiveLimitation(const DsaProblem& problem) {
  std::optional<std::size_t> reuse_index;
  std::optional<std::size_t> peak_index;
  for (std::size_t index = 0; index < problem.objective.terms.size(); ++index) {
    const ObjectiveMetric metric = problem.objective.terms[index];
    if (metric == ObjectiveMetric::kReuseCost) {
      reuse_index = index;
    } else if ((metric == ObjectiveMetric::kTotalPeak || metric == ObjectiveMetric::kMaxPeak) &&
               !peak_index.has_value()) {
      peak_index = index;
    }
  }
  if (!reuse_index.has_value()) {
    return "treewidth_partition_dp requires reuse_cost in the objective";
  }
  if (reuse_index.has_value() && peak_index.has_value() &&
      peak_index.value() < reuse_index.value()) {
    return "treewidth_partition_dp requires reuse_cost before peak metrics";
  }
  return std::nullopt;
}

std::vector<std::size_t> PoolNodes(const detail::ReusePenaltySearchSpace& search, PoolId pool) {
  std::vector<std::size_t> nodes;
  for (std::size_t node = 0; node < search.placement.nodes.size(); ++node) {
    if (search.placement.nodes[node].pool == pool) {
      nodes.push_back(node);
    }
  }
  return nodes;
}

std::map<detail::ReuseNodePair, PairPotential> BuildPotentials(
    const detail::ReusePenaltySearchSpace& search, const std::vector<std::size_t>& nodes) {
  std::set<std::size_t> in_pool(nodes.begin(), nodes.end());
  std::map<detail::ReuseNodePair, PairPotential> potentials;
  for (std::size_t node : nodes) {
    for (std::size_t neighbor : search.hard_neighbors[node]) {
      if (node < neighbor && in_pool.count(neighbor) != 0) {
        potentials[{node, neighbor}].hard = true;
      }
    }
  }
  for (const auto& [pair, weight] : search.soft_weights) {
    if (in_pool.count(pair.first) != 0 && in_pool.count(pair.second) != 0) {
      potentials[pair].soft_weight = weight;
    }
  }
  return potentials;
}

EliminationOrder MinFillOrder(const detail::ReusePenaltySearchSpace& search,
                              const std::vector<std::size_t>& nodes,
                              const std::map<detail::ReuseNodePair, PairPotential>& potentials) {
  std::vector<std::set<std::size_t>> adjacency(search.placement.nodes.size());
  for (const auto& [pair, potential] : potentials) {
    static_cast<void>(potential);
    adjacency[pair.first].insert(pair.second);
    adjacency[pair.second].insert(pair.first);
  }
  std::set<std::size_t> active(nodes.begin(), nodes.end());
  EliminationOrder result;
  result.variables.reserve(nodes.size());
  while (!active.empty()) {
    std::optional<std::tuple<std::uint64_t, std::size_t, BufferId, std::size_t>> best;
    for (std::size_t node : active) {
      std::vector<std::size_t> neighbors;
      for (std::size_t neighbor : adjacency[node]) {
        if (active.count(neighbor) != 0) {
          neighbors.push_back(neighbor);
        }
      }
      std::uint64_t fill = 0;
      for (std::size_t first = 0; first < neighbors.size(); ++first) {
        for (std::size_t second = first + 1; second < neighbors.size(); ++second) {
          if (adjacency[neighbors[first]].count(neighbors[second]) == 0) {
            ++fill;
          }
        }
      }
      const auto key = std::make_tuple(fill, neighbors.size(),
                                       search.placement.nodes[node].representative, node);
      if (!best.has_value() || key < best.value()) {
        best = key;
      }
    }
    if (!best.has_value()) {
      break;
    }
    const std::size_t selected = std::get<3>(*best);
    std::vector<std::size_t> neighbors;
    for (std::size_t neighbor : adjacency[selected]) {
      if (active.count(neighbor) != 0) {
        neighbors.push_back(neighbor);
      }
    }
    result.width = std::max(result.width, neighbors.size());
    for (std::size_t first = 0; first < neighbors.size(); ++first) {
      for (std::size_t second = first + 1; second < neighbors.size(); ++second) {
        adjacency[neighbors[first]].insert(neighbors[second]);
        adjacency[neighbors[second]].insert(neighbors[first]);
      }
      adjacency[neighbors[first]].erase(selected);
    }
    active.erase(selected);
    result.variables.push_back(selected);
  }
  return result;
}

Partition CanonicalizePartition(const Partition& labels) {
  std::map<std::uint32_t, std::uint32_t> canonical;
  Partition result;
  result.reserve(labels.size());
  for (std::uint32_t label : labels) {
    const auto [found, inserted] =
        canonical.emplace(label, static_cast<std::uint32_t>(canonical.size()));
    static_cast<void>(inserted);
    result.push_back(found->second);
  }
  return result;
}

template <typename Callback>
bool EnumeratePartitions(std::size_t size, std::uint32_t max_blocks, Callback callback) {
  Partition partition(size, 0);
  if (size == 0) return callback(partition);
  bool keep_going = true;
  const auto recurse = [&](const auto& self, std::size_t position, std::uint32_t blocks) -> void {
    if (!keep_going) return;
    if (position == size) {
      keep_going = callback(partition);
      return;
    }
    const std::uint32_t choices = std::min(max_blocks, blocks + 1);
    for (std::uint32_t block = 0; block < choices; ++block) {
      partition[position] = block;
      self(self, position + 1, std::max(blocks, block + 1));
      if (!keep_going) return;
    }
  };
  partition[0] = 0;
  recurse(recurse, 1, 1);
  return keep_going;
}

TableValue LookupFactor(const Factor& factor, const std::vector<std::uint32_t>& colors) {
  Partition labels;
  labels.reserve(factor.scope.size());
  for (std::size_t variable : factor.scope) {
    labels.push_back(colors[variable]);
  }
  const auto found = factor.values.find(CanonicalizePartition(labels));
  return found == factor.values.end() ? TableValue{0, false} : found->second;
}

TableValue SumBucket(const std::vector<Factor>& bucket, const std::vector<std::uint32_t>& colors) {
  TableValue result;
  for (const Factor& factor : bucket) {
    const TableValue value = LookupFactor(factor, colors);
    if (!value.feasible) {
      return {0, false};
    }
    result.cost = detail::SaturatingAdd(result.cost, value.cost);
  }
  return result;
}

Factor PairFactor(std::size_t first, std::size_t second, const PairPotential& potential,
                  std::uint64_t colors) {
  Factor factor;
  factor.scope = {first, second};
  factor.values[{0, 0}] = {potential.soft_weight, !potential.hard};
  if (colors >= 2) {
    factor.values[{0, 1}] = {0, true};
  }
  return factor;
}

PoolDpResult SolvePool(const detail::ReusePenaltySearchSpace& search, const Pool& pool,
                       const TreewidthPartitionDpOptions& options) {
  PoolDpResult result;
  result.offsets.assign(search.placement.nodes.size(), 0);
  if (!pool.capacity.has_value()) {
    result.diagnostic = "treewidth_partition_dp requires fixed pool capacities";
    return result;
  }
  if (!pool.reserved_ranges.empty()) {
    result.diagnostic = "treewidth_partition_dp does not support reserved ranges";
    return result;
  }
  const std::vector<std::size_t> nodes = PoolNodes(search, pool.id);
  if (nodes.empty()) {
    result.status = SolveStatus::kFeasible;
    return result;
  }
  const std::uint64_t unit = search.placement.nodes[nodes.front()].size;
  for (std::size_t node : nodes) {
    const detail::PlacementSearchNode& value = search.placement.nodes[node];
    if (value.size != unit || unit % value.alignment != 0) {
      result.diagnostic =
          "treewidth_partition_dp requires uniform unit sizes and "
          "compatible alignment";
      return result;
    }
  }
  if (unit == 0) return result;
  const std::uint64_t colors = std::min<std::uint64_t>(pool.capacity.value() / unit, nodes.size());
  if (colors == 0 || colors > std::numeric_limits<std::uint32_t>::max()) {
    result.status = SolveStatus::kInfeasibleProven;
    result.diagnostic = "treewidth_partition_dp has no representable address colors";
    return result;
  }

  const auto potentials = BuildPotentials(search, nodes);
  const EliminationOrder order = MinFillOrder(search, nodes, potentials);
  result.width = order.width;
  if (order.width > options.max_treewidth) {
    result.diagnostic = "treewidth_partition_dp min-fill width exceeds max_treewidth";
    return result;
  }

  std::vector<Factor> factors;
  factors.reserve(potentials.size() + nodes.size());
  for (const auto& [pair, potential] : potentials) {
    factors.push_back(PairFactor(pair.first, pair.second, potential, colors));
  }

  std::vector<Trace> traces;
  traces.reserve(nodes.size());
  std::vector<std::uint32_t> working_colors(search.placement.nodes.size(), 0);
  for (std::size_t variable : order.variables) {
    std::vector<Factor> bucket;
    std::vector<Factor> remaining;
    for (Factor& factor : factors) {
      if (std::find(factor.scope.begin(), factor.scope.end(), variable) != factor.scope.end()) {
        bucket.push_back(std::move(factor));
      } else {
        remaining.push_back(std::move(factor));
      }
    }

    std::set<std::size_t> remaining_variables;
    for (const Factor& factor : bucket) {
      for (std::size_t member : factor.scope) {
        if (member != variable) {
          remaining_variables.insert(member);
        }
      }
    }
    Trace trace;
    trace.variable = variable;
    trace.remaining_scope.assign(remaining_variables.begin(), remaining_variables.end());
    Factor output;
    output.scope = trace.remaining_scope;
    std::uint64_t table_entries = 0;
    const bool completed = EnumeratePartitions(
        trace.remaining_scope.size(), static_cast<std::uint32_t>(colors),
        [&](const Partition& partition) {
          ++table_entries;
          if (options.max_table_entries != 0 && table_entries > options.max_table_entries) {
            return false;
          }
          for (std::size_t index = 0; index < trace.remaining_scope.size(); ++index) {
            working_colors[trace.remaining_scope[index]] = partition[index];
          }
          const std::uint32_t existing_blocks =
              partition.empty() ? 0 : *std::max_element(partition.begin(), partition.end()) + 1;
          TableValue best{0, false};
          std::uint32_t best_block = 0;
          const std::uint32_t choices = existing_blocks + (existing_blocks < colors ? 1U : 0U);
          for (std::uint32_t block = 0; block < choices; ++block) {
            working_colors[variable] = block;
            const TableValue candidate = SumBucket(bucket, working_colors);
            ++result.transitions;
            if (candidate.feasible && (!best.feasible || candidate.cost < best.cost)) {
              best = candidate;
              best_block = block;
            }
          }
          output.values[partition] = best;
          trace.best_block[partition] = best_block;
          ++result.states;
          return true;
        });
    if (!completed) {
      result.diagnostic = "treewidth_partition_dp partition table exceeds max_table_entries";
      return result;
    }
    remaining.push_back(std::move(output));
    factors = std::move(remaining);
    traces.push_back(std::move(trace));
  }

  TableValue optimum;
  for (const Factor& factor : factors) {
    const Partition empty_partition;
    if (!factor.scope.empty() || factor.values.size() != 1 ||
        factor.values.find(empty_partition) == factor.values.end()) {
      result.diagnostic = "treewidth_partition_dp left a nonconstant terminal factor";
      return result;
    }
    if (!factor.values.at(empty_partition).feasible) {
      result.status = SolveStatus::kInfeasibleProven;
      return result;
    }
    optimum.cost = detail::SaturatingAdd(optimum.cost, factor.values.at(empty_partition).cost);
  }

  const std::uint32_t unassigned = std::numeric_limits<std::uint32_t>::max();
  std::vector<std::uint32_t> colors_by_node(search.placement.nodes.size(), unassigned);
  for (auto iterator = traces.rbegin(); iterator != traces.rend(); ++iterator) {
    Partition actual;
    actual.reserve(iterator->remaining_scope.size());
    for (std::size_t member : iterator->remaining_scope) {
      actual.push_back(colors_by_node[member]);
    }
    const Partition key = CanonicalizePartition(actual);
    const auto choice = iterator->best_block.find(key);
    if (choice == iterator->best_block.end()) {
      result.diagnostic = "treewidth_partition_dp traceback state is missing";
      return result;
    }
    const std::uint32_t existing_blocks =
        key.empty() ? 0 : *std::max_element(key.begin(), key.end()) + 1;
    if (choice->second < existing_blocks) {
      bool assigned = false;
      for (std::size_t index = 0; index < key.size(); ++index) {
        if (key[index] == choice->second) {
          colors_by_node[iterator->variable] = actual[index];
          assigned = true;
          break;
        }
      }
      if (!assigned) {
        result.diagnostic = "treewidth_partition_dp traceback block is missing";
        return result;
      }
    } else {
      std::set<std::uint32_t> used(actual.begin(), actual.end());
      std::uint32_t color = 0;
      while (used.count(color) != 0) ++color;
      if (color >= colors) {
        result.diagnostic = "treewidth_partition_dp traceback exceeds capacity colors";
        return result;
      }
      colors_by_node[iterator->variable] = color;
    }
  }
  for (std::size_t node : nodes) {
    result.offsets[node] = static_cast<std::uint64_t>(colors_by_node[node]) * unit;
  }
  result.reuse_cost = optimum.cost;
  result.status = SolveStatus::kFeasible;
  return result;
}

}  // namespace

TreewidthPartitionDpSolver::TreewidthPartitionDpSolver(TreewidthPartitionDpOptions options)
    : options_(options) {}

const char* TreewidthPartitionDpSolver::Name() const noexcept { return "treewidth_partition_dp"; }

SolverCapabilities TreewidthPartitionDpSolver::Capabilities() const noexcept {
  return TreewidthCapabilities();
}

DsaResult TreewidthPartitionDpSolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    DsaResult result;
    const std::vector<std::string> errors = ValidateProblem(problem);
    if (!errors.empty()) {
      result.status = SolveStatus::kInvalidProblem;
      result.diagnostics = errors;
      return result;
    }
    result.status = SolveStatus::kUnsupported;
    for (const std::string& feature : compatibility.unsupported_features) {
      result.diagnostics.emplace_back(std::string(Name()) + " does not support feature '" +
                                      feature + "'");
    }
    for (const std::string& objective : compatibility.unsupported_objectives) {
      result.diagnostics.emplace_back(std::string(Name()) + " does not support objective '" +
                                      objective + "'");
    }
    return result;
  }
  if (const auto limitation = ObjectiveLimitation(problem); limitation.has_value()) {
    DsaResult result;
    result.status = SolveStatus::kUnsupported;
    result.diagnostics.push_back(limitation.value());
    return result;
  }

  const detail::ReusePenaltySearchSpace search = detail::BuildReusePenaltySearchSpace(problem);
  if (!search.placement.errors.empty()) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = search.placement.errors;
    return result;
  }
  std::vector<std::uint64_t> offsets(search.placement.nodes.size(), 0);
  std::uint64_t max_width = 0;
  std::uint64_t states = 0;
  std::uint64_t transitions = 0;
  std::uint64_t expected_reuse_cost = 0;
  for (const Pool& pool : problem.pools) {
    const PoolDpResult pool_result = SolvePool(search, pool, options_);
    if (pool_result.status != SolveStatus::kFeasible) {
      DsaResult result;
      result.status = pool_result.status;
      if (!pool_result.diagnostic.empty()) {
        result.diagnostics.push_back(pool_result.diagnostic);
      }
      return result;
    }
    max_width = std::max<std::uint64_t>(max_width, pool_result.width);
    states = detail::SaturatingAdd(states, pool_result.states);
    transitions = detail::SaturatingAdd(transitions, pool_result.transitions);
    expected_reuse_cost = detail::SaturatingAdd(expected_reuse_cost, pool_result.reuse_cost);
    for (std::size_t node = 0; node < offsets.size(); ++node) {
      if (search.placement.nodes[node].pool == pool.id) {
        offsets[node] = pool_result.offsets[node];
      }
    }
  }

  DsaResult result =
      detail::BuildValidatedReuseResult(problem, search, offsets, SolveStatus::kFeasible);
  if (result.status == SolveStatus::kFeasible &&
      result.objective.reuse_cost != expected_reuse_cost) {
    result.status = SolveStatus::kInvalidProblem;
    result.solution.reset();
    result.diagnostics.emplace_back(
        "treewidth_partition_dp objective reconstruction disagrees with "
        "the dynamic program");
    return result;
  }
  result.solver_metrics = {
      {"min_fill_width", max_width},
      {"dp_states", states},
      {"dp_transitions", transitions},
      {"optimal_reuse_cost_proven", 1},
  };
  result.diagnostics.emplace_back(
      "treewidth_partition_dp proved minimum reuse cost by exact "
      "min-sum variable elimination; peak tie-breaking is not optimized");
  return result;
}

}  // namespace dsa

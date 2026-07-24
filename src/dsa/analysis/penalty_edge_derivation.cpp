// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/analysis/penalty_edge_derivation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dsa/model/model.h"
#include "dsa/model/validator.h"

namespace dsa {
namespace {

struct IndexedProgram {
  std::unordered_map<OperationId, std::size_t> operation_index;
  std::unordered_map<BufferId, std::vector<std::size_t>> accesses_by_buffer;
  std::vector<std::vector<std::size_t>> successors;
  std::vector<std::vector<std::uint64_t>> vector_clocks;
};

std::uint64_t SaturatingAdd(std::uint64_t first, std::uint64_t second) {
  return first > std::numeric_limits<std::uint64_t>::max() - second
             ? std::numeric_limits<std::uint64_t>::max()
             : first + second;
}

std::pair<std::int64_t, std::int64_t> LifetimeHull(const Buffer& buffer) {
  std::int64_t lower = std::numeric_limits<std::int64_t>::max();
  std::int64_t upper = std::numeric_limits<std::int64_t>::min();
  for (const Interval& interval : buffer.live_intervals) {
    lower = std::min(lower, interval.lower);
    upper = std::max(upper, interval.upper);
  }
  return {lower, upper};
}

bool EarlierLifetime(const Buffer& first, const Buffer& second) {
  const auto [first_lower, first_upper] = LifetimeHull(first);
  const auto [second_lower, second_upper] = LifetimeHull(second);
  static_cast<void>(first_lower);
  static_cast<void>(second_upper);
  return first_upper <= second_lower;
}

IndexedProgram BuildIndex(const DsaProblem& problem, const ScheduledProgram& program) {
  if (program.stream_count == 0) {
    throw std::invalid_argument("scheduled program has no streams");
  }
  if (program.operations.empty()) {
    throw std::invalid_argument("scheduled program has no operations");
  }

  IndexedProgram indexed;
  indexed.successors.resize(program.operations.size());
  std::vector<std::size_t> indegree(program.operations.size(), 0);
  std::vector<std::vector<std::size_t>> operations_by_stream(program.stream_count);
  for (std::size_t index = 0; index < program.operations.size(); ++index) {
    const ScheduledOperation& operation = program.operations[index];
    if (operation.stream >= program.stream_count) {
      throw std::invalid_argument("operation " + std::to_string(operation.id) +
                                  " references an unknown stream");
    }
    if (!indexed.operation_index.emplace(operation.id, index).second) {
      throw std::invalid_argument("duplicate operation id " + std::to_string(operation.id));
    }
    operations_by_stream[operation.stream].push_back(index);
  }

  auto add_edge = [&](std::size_t before, std::size_t after) {
    if (before == after) {
      throw std::invalid_argument("scheduled dependency is a self edge");
    }
    if (program.operations[before].schedule_time > program.operations[after].schedule_time) {
      throw std::invalid_argument("scheduled dependency violates temporal consistency");
    }
    indexed.successors[before].push_back(after);
    ++indegree[after];
  };

  for (std::vector<std::size_t>& stream_operations : operations_by_stream) {
    std::sort(stream_operations.begin(), stream_operations.end(),
              [&](std::size_t first, std::size_t second) {
                const ScheduledOperation& first_op = program.operations[first];
                const ScheduledOperation& second_op = program.operations[second];
                return std::tie(first_op.issue_index, first_op.id) <
                       std::tie(second_op.issue_index, second_op.id);
              });
    for (std::size_t index = 1; index < stream_operations.size(); ++index) {
      const ScheduledOperation& previous = program.operations[stream_operations[index - 1]];
      const ScheduledOperation& current = program.operations[stream_operations[index]];
      if (previous.issue_index == current.issue_index) {
        throw std::invalid_argument("stream repeats issue index " +
                                    std::to_string(current.issue_index));
      }
      add_edge(stream_operations[index - 1], stream_operations[index]);
    }
  }
  for (const ScheduledDependency& dependency : program.cross_dependencies) {
    const auto before = indexed.operation_index.find(dependency.before);
    const auto after = indexed.operation_index.find(dependency.after);
    if (before == indexed.operation_index.end() || after == indexed.operation_index.end()) {
      throw std::invalid_argument("scheduled dependency references an unknown operation");
    }
    add_edge(before->second, after->second);
  }

  std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
  for (std::size_t index = 0; index < indegree.size(); ++index) {
    if (indegree[index] == 0) ready.push(index);
  }
  std::vector<std::size_t> topological_order;
  topological_order.reserve(program.operations.size());
  while (!ready.empty()) {
    const std::size_t current = ready.top();
    ready.pop();
    topological_order.push_back(current);
    for (std::size_t successor : indexed.successors[current]) {
      if (--indegree[successor] == 0) ready.push(successor);
    }
  }
  if (topological_order.size() != program.operations.size()) {
    throw std::invalid_argument("scheduled program dependency graph is cyclic");
  }

  indexed.vector_clocks.assign(program.operations.size(),
                               std::vector<std::uint64_t>(program.stream_count, 0));
  std::vector<std::vector<std::size_t>> predecessors(program.operations.size());
  for (std::size_t before = 0; before < indexed.successors.size(); ++before) {
    for (std::size_t after : indexed.successors[before]) {
      predecessors[after].push_back(before);
    }
  }
  for (std::size_t operation_index : topological_order) {
    std::vector<std::uint64_t>& clock = indexed.vector_clocks[operation_index];
    for (std::size_t predecessor : predecessors[operation_index]) {
      for (std::size_t stream = 0; stream < program.stream_count; ++stream) {
        clock[stream] = std::max(clock[stream], indexed.vector_clocks[predecessor][stream]);
      }
    }
    const ScheduledOperation& operation = program.operations[operation_index];
    if (operation.issue_index == std::numeric_limits<std::uint64_t>::max()) {
      throw std::invalid_argument("operation issue index cannot be UINT64_MAX");
    }
    clock[operation.stream] = std::max(clock[operation.stream], operation.issue_index + 1);
  }

  for (std::size_t access_index = 0; access_index < program.accesses.size(); ++access_index) {
    const ScheduledBufferAccess& access = program.accesses[access_index];
    if (problem.FindBuffer(access.buffer) == nullptr) {
      throw std::invalid_argument("access references unknown buffer " +
                                  std::to_string(access.buffer));
    }
    if (indexed.operation_index.count(access.operation) == 0) {
      throw std::invalid_argument("access references unknown operation " +
                                  std::to_string(access.operation));
    }
    indexed.accesses_by_buffer[access.buffer].push_back(access_index);
  }
  for (const Buffer& buffer : problem.buffers) {
    if (indexed.accesses_by_buffer.count(buffer.id) == 0) {
      throw std::invalid_argument("buffer " + std::to_string(buffer.id) +
                                  " has no scheduled accesses");
    }
  }
  return indexed;
}

bool Ordered(const ScheduledProgram& program, const IndexedProgram& indexed, std::size_t before,
             std::size_t after) {
  if (before == after) return true;
  const ScheduledOperation& operation = program.operations[before];
  return indexed.vector_clocks[after][operation.stream] > operation.issue_index;
}

std::size_t FirstWrite(const ScheduledProgram& program, const IndexedProgram& indexed,
                       BufferId buffer) {
  const std::vector<std::size_t>& accesses = indexed.accesses_by_buffer.at(buffer);
  std::optional<std::size_t> first_write;
  for (std::size_t candidate : accesses) {
    if (program.accesses[candidate].kind != AccessKind::kWrite) continue;
    const std::size_t candidate_op =
        indexed.operation_index.at(program.accesses[candidate].operation);
    const bool defines_every_access =
        std::all_of(accesses.begin(), accesses.end(), [&](std::size_t access) {
          return Ordered(program, indexed, candidate_op,
                         indexed.operation_index.at(program.accesses[access].operation));
        });
    if (!defines_every_access) continue;
    if (!first_write) {
      first_write = candidate;
      continue;
    }
    if (program.accesses[*first_write].operation != program.accesses[candidate].operation) {
      throw std::invalid_argument("buffer " + std::to_string(buffer) +
                                  " has multiple first-write operations");
    }
  }
  if (!first_write) {
    throw std::invalid_argument("buffer " + std::to_string(buffer) +
                                " has no first write that happens-before every access");
  }
  return *first_write;
}

std::vector<std::size_t> MaximalAccesses(const ScheduledProgram& program,
                                         const IndexedProgram& indexed, BufferId buffer) {
  std::map<StreamId, std::size_t> last_by_stream;
  for (std::size_t access_index : indexed.accesses_by_buffer.at(buffer)) {
    const ScheduledOperation& operation =
        program.operations[indexed.operation_index.at(program.accesses[access_index].operation)];
    const auto current = last_by_stream.find(operation.stream);
    if (current == last_by_stream.end()) {
      last_by_stream.emplace(operation.stream, access_index);
      continue;
    }
    const ScheduledOperation& previous =
        program.operations[indexed.operation_index.at(program.accesses[current->second].operation)];
    if (std::tie(previous.issue_index, previous.id) <
        std::tie(operation.issue_index, operation.id)) {
      current->second = access_index;
    }
  }

  std::vector<std::size_t> candidates;
  for (const auto& [stream, access_index] : last_by_stream) {
    static_cast<void>(stream);
    candidates.push_back(access_index);
  }
  std::vector<std::size_t> maximal;
  for (std::size_t candidate : candidates) {
    const std::size_t candidate_op =
        indexed.operation_index.at(program.accesses[candidate].operation);
    const bool precedes_another =
        std::any_of(candidates.begin(), candidates.end(), [&](std::size_t other) {
          if (candidate == other) return false;
          return Ordered(program, indexed, candidate_op,
                         indexed.operation_index.at(program.accesses[other].operation));
        });
    if (!precedes_another) maximal.push_back(candidate);
  }
  return maximal;
}

}  // namespace

PenaltyEdgeDerivation DerivePenaltyEdges(const DsaProblem& problem, const ScheduledProgram& program,
                                         const std::vector<std::vector<std::uint64_t>>& sync_cost) {
  const std::vector<std::string> problem_errors = ValidateProblem(problem);
  if (!problem_errors.empty()) {
    throw std::invalid_argument("cannot derive penalties for an invalid DSA problem: " +
                                problem_errors.front());
  }
  if (sync_cost.size() != program.stream_count ||
      std::any_of(sync_cost.begin(), sync_cost.end(),
                  [&](const auto& row) { return row.size() != program.stream_count; })) {
    throw std::invalid_argument("synchronization-cost matrix does not match stream count");
  }
  const IndexedProgram indexed = BuildIndex(problem, program);

  std::unordered_map<BufferId, std::size_t> first_access_by_buffer;
  std::unordered_map<BufferId, std::vector<std::size_t>> maximal_by_buffer;
  for (const Buffer& buffer : problem.buffers) {
    const std::size_t first_access = FirstWrite(program, indexed, buffer.id);
    first_access_by_buffer.emplace(buffer.id, first_access);
    maximal_by_buffer.emplace(buffer.id, MaximalAccesses(program, indexed, buffer.id));
  }

  PenaltyEdgeDerivation derivation;
  for (std::size_t first_index = 0; first_index < problem.buffers.size(); ++first_index) {
    for (std::size_t second_index = first_index + 1; second_index < problem.buffers.size();
         ++second_index) {
      const Buffer* earlier = &problem.buffers[first_index];
      const Buffer* later = &problem.buffers[second_index];
      if (EarlierLifetime(*later, *earlier)) std::swap(earlier, later);
      if (!EarlierLifetime(*earlier, *later)) continue;
      ++derivation.lifetime_compatible_pairs;

      const std::size_t first_write_access = first_access_by_buffer.at(later->id);
      const std::size_t first_write_op =
          indexed.operation_index.at(program.accesses[first_write_access].operation);
      const StreamId first_write_stream = program.operations[first_write_op].stream;
      DerivedPenaltyEdge edge;
      edge.first = std::min(earlier->id, later->id);
      edge.second = std::max(earlier->id, later->id);
      for (std::size_t maximal_access : maximal_by_buffer.at(earlier->id)) {
        const std::size_t maximal_op =
            indexed.operation_index.at(program.accesses[maximal_access].operation);
        if (Ordered(program, indexed, maximal_op, first_write_op)) continue;
        const StreamId maximal_stream = program.operations[maximal_op].stream;
        const std::uint64_t cost = sync_cost[maximal_stream][first_write_stream];
        if (cost == 0) continue;
        edge.cost = SaturatingAdd(edge.cost, cost);
        edge.needed_syncs.push_back(
            {program.operations[maximal_op].id, program.operations[first_write_op].id});
      }
      if (edge.needed_syncs.empty()) {
        ++derivation.ordered_pairs;
      } else {
        derivation.edges.push_back(std::move(edge));
      }
    }
  }
  std::sort(derivation.edges.begin(), derivation.edges.end(),
            [](const DerivedPenaltyEdge& first, const DerivedPenaltyEdge& second) {
              return std::tie(first.first, first.second) < std::tie(second.first, second.second);
            });
  return derivation;
}

void ApplyPenaltyEdgeDerivation(DsaProblem* problem, const PenaltyEdgeDerivation& derivation) {
  if (problem == nullptr) throw std::invalid_argument("problem must not be null");
  CostModel cost_model = problem->cost_model.value_or(CostModel{});
  cost_model.reuse_penalties.reserve(cost_model.reuse_penalties.size() + derivation.edges.size());
  for (const DerivedPenaltyEdge& edge : derivation.edges) {
    cost_model.reuse_penalties.push_back(
        {edge.first, edge.second, edge.cost, ReusePenaltyReason::kGeneric});
  }
  problem->cost_model = std::move(cost_model);
  problem->objective = FitThenMinimizeReuseCostObjective();
}

}  // namespace dsa

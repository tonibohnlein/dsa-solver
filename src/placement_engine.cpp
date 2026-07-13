#include "detail/placement_engine.h"

#include <algorithm>
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

#include "dsa/validator.h"

namespace dsa::detail {
namespace {

class DisjointSet {
 public:
  explicit DisjointSet(std::size_t size) : parent_(size) {
    for (std::size_t i = 0; i < size; ++i) parent_[i] = i;
  }

  std::size_t Find(std::size_t value) {
    if (parent_[value] != value) parent_[value] = Find(parent_[value]);
    return parent_[value];
  }

  void Union(std::size_t first, std::size_t second) {
    const std::size_t first_root = Find(first);
    const std::size_t second_root = Find(second);
    if (first_root != second_root) parent_[second_root] = first_root;
  }

 private:
  std::vector<std::size_t> parent_;
};

struct SuperBuffer {
  std::size_t root = 0;
  BufferId representative = 0;
  std::vector<std::size_t> members;
  std::uint64_t size = 0;
  std::uint64_t alignment = 1;
  std::vector<Interval> live_intervals;
  std::vector<PoolId> allowed_pools;
  std::optional<PinnedAllocation> pinned;
  bool pinned_exclusive = false;

  PoolId placed_pool = kDefaultPool;
  std::uint64_t offset = 0;
};

struct PreparedProblem {
  std::vector<SuperBuffer> supers;
  std::unordered_map<BufferId, std::size_t> id_to_super;
  std::set<std::pair<std::size_t, std::size_t>> separations;
  std::vector<std::string> errors;
  bool flexible_pools = false;
};

struct FitRequest {
  std::uint64_t size = 0;
  std::uint64_t alignment = 1;
};

bool AddOverflows(std::uint64_t first, std::uint64_t second) {
  return first > std::numeric_limits<std::uint64_t>::max() - second;
}

std::optional<std::uint64_t> AlignUp(std::uint64_t value, std::uint64_t alignment) {
  if (alignment <= 1) return value;
  const std::uint64_t remainder = value % alignment;
  if (remainder == 0) return value;
  const std::uint64_t delta = alignment - remainder;
  if (AddOverflows(value, delta)) return std::nullopt;
  return value + delta;
}

bool SuperBuffersConflict(const DsaProblem& problem, const SuperBuffer& first,
                          const SuperBuffer& second) {
  for (std::size_t first_member : first.members) {
    for (std::size_t second_member : second.members) {
      if (HaveTemporalConflict(problem, problem.buffers[first_member],
                               problem.buffers[second_member])) {
        return true;
      }
    }
  }
  return false;
}

std::vector<PoolId> IntersectPools(const std::vector<PoolId>& first,
                                   const std::vector<PoolId>& second) {
  std::vector<PoolId> first_sorted = first;
  std::vector<PoolId> second_sorted = second;
  std::sort(first_sorted.begin(), first_sorted.end());
  std::sort(second_sorted.begin(), second_sorted.end());
  std::vector<PoolId> result;
  std::set_intersection(first_sorted.begin(), first_sorted.end(), second_sorted.begin(),
                        second_sorted.end(), std::back_inserter(result));
  return result;
}

PreparedProblem Prepare(const DsaProblem& problem) {
  PreparedProblem prepared;
  prepared.errors = ValidateProblem(problem);
  if (!prepared.errors.empty()) return prepared;

  std::unordered_map<BufferId, std::size_t> id_to_index;
  for (std::size_t i = 0; i < problem.buffers.size(); ++i) {
    id_to_index.emplace(problem.buffers[i].id, i);
  }

  DisjointSet sets(problem.buffers.size());
  for (const Colocation& colocation : problem.colocations) {
    sets.Union(id_to_index.at(colocation.first), id_to_index.at(colocation.second));
  }

  std::map<std::size_t, std::size_t> root_to_super;
  for (std::size_t i = 0; i < problem.buffers.size(); ++i) {
    const std::size_t root = sets.Find(i);
    auto [it, inserted] = root_to_super.emplace(root, prepared.supers.size());
    if (inserted) {
      SuperBuffer super;
      super.root = root;
      super.representative = problem.buffers[i].id;
      super.allowed_pools = problem.buffers[i].allowed_pools;
      prepared.supers.push_back(std::move(super));
    }
    SuperBuffer& super = prepared.supers[it->second];
    const Buffer& buffer = problem.buffers[i];
    super.members.push_back(i);
    super.representative = std::min(super.representative, buffer.id);
    super.size = std::max(super.size, buffer.size);
    super.alignment = std::max(super.alignment, buffer.alignment);
    super.live_intervals.insert(super.live_intervals.end(), buffer.live_intervals.begin(),
                                buffer.live_intervals.end());
    if (!inserted) {
      super.allowed_pools = IntersectPools(super.allowed_pools, buffer.allowed_pools);
    }
  }

  for (std::size_t super_index = 0; super_index < prepared.supers.size(); ++super_index) {
    SuperBuffer& super = prepared.supers[super_index];
    if (super.allowed_pools.empty()) {
      prepared.errors.push_back("colocation class has no common allowed pool");
    } else if (super.allowed_pools.size() > 1) {
      prepared.flexible_pools = true;
    }
    for (std::size_t member : super.members) {
      prepared.id_to_super[problem.buffers[member].id] = super_index;
    }
  }

  for (const PinnedAllocation& pinned : problem.pinned_allocations) {
    SuperBuffer& super = prepared.supers.at(prepared.id_to_super.at(pinned.buffer));
    if (super.pinned &&
        (super.pinned->pool != pinned.pool || super.pinned->offset != pinned.offset)) {
      prepared.errors.push_back("colocated buffers have incompatible pinned placements");
    } else {
      super.pinned = pinned;
      super.pinned_exclusive = super.pinned_exclusive || pinned.exclusive_for_all_time;
    }
  }

  for (const Separation& separation : problem.separations) {
    std::size_t first = prepared.id_to_super.at(separation.first);
    std::size_t second = prepared.id_to_super.at(separation.second);
    if (first == second) {
      prepared.errors.push_back("separation lies within a colocation class");
      continue;
    }
    if (first > second) std::swap(first, second);
    prepared.separations.emplace(first, second);
  }
  return prepared;
}

bool Separated(const PreparedProblem& prepared, std::size_t first, std::size_t second) {
  if (first > second) std::swap(first, second);
  return prepared.separations.count({first, second}) != 0;
}

std::optional<std::uint64_t> FindFirstFit(FitRequest request,
                                          std::vector<AddressRange> blocked_ranges) {
  std::sort(blocked_ranges.begin(), blocked_ranges.end(),
            [](const AddressRange& first, const AddressRange& second) {
              return std::tie(first.begin, first.end) < std::tie(second.begin, second.end);
            });

  std::optional<std::uint64_t> candidate = AlignUp(0, request.alignment);
  if (!candidate) return std::nullopt;
  for (const AddressRange& blocked : blocked_ranges) {
    if (AddOverflows(*candidate, request.size)) return std::nullopt;
    if (blocked.begin >= *candidate + request.size) break;
    if (blocked.end > *candidate) {
      candidate = AlignUp(blocked.end, request.alignment);
      if (!candidate) return std::nullopt;
    }
  }
  if (AddOverflows(*candidate, request.size)) return std::nullopt;
  return candidate;
}

std::vector<std::size_t> SortedSuperOrder(const DsaProblem& problem,
                                          const PreparedProblem& prepared,
                                          const std::vector<BufferId>& priority) {
  std::unordered_map<std::size_t, std::size_t> rank;
  for (std::size_t i = 0; i < priority.size(); ++i) {
    const auto found = prepared.id_to_super.find(priority[i]);
    if (found != prepared.id_to_super.end()) rank.emplace(found->second, i);
  }

  std::vector<std::size_t> order(prepared.supers.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](std::size_t first, std::size_t second) {
    const auto first_rank = rank.find(first);
    const auto second_rank = rank.find(second);
    if (first_rank != rank.end() || second_rank != rank.end()) {
      if (first_rank == rank.end()) return false;
      if (second_rank == rank.end()) return true;
      if (first_rank->second != second_rank->second)
        return first_rank->second < second_rank->second;
    }
    const SuperBuffer& first_super = prepared.supers[first];
    const SuperBuffer& second_super = prepared.supers[second];
    if (first_super.size != second_super.size) return first_super.size > second_super.size;
    const auto first_lower =
        std::min_element(first_super.live_intervals.begin(), first_super.live_intervals.end(),
                         [](const Interval& a, const Interval& b) { return a.lower < b.lower; });
    const auto second_lower =
        std::min_element(second_super.live_intervals.begin(), second_super.live_intervals.end(),
                         [](const Interval& a, const Interval& b) { return a.lower < b.lower; });
    if (first_lower->lower != second_lower->lower) return first_lower->lower < second_lower->lower;
    return first_super.representative < second_super.representative;
  });
  static_cast<void>(problem);
  return order;
}

}  // namespace

std::vector<BufferId> DefaultPlacementOrder(const DsaProblem& problem) {
  const PreparedProblem prepared = Prepare(problem);
  if (!prepared.errors.empty() || prepared.flexible_pools) return {};
  const std::vector<std::size_t> order = SortedSuperOrder(problem, prepared, {});
  std::vector<BufferId> result;
  result.reserve(order.size());
  for (std::size_t index : order) result.push_back(prepared.supers[index].representative);
  return result;
}

DsaResult PlaceWithOrder(const DsaProblem& problem, const std::vector<BufferId>& priority) {
  DsaResult result;
  PreparedProblem prepared = Prepare(problem);
  if (!prepared.errors.empty()) {
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = std::move(prepared.errors);
    return result;
  }
  if (prepared.flexible_pools) {
    result.status = SolveStatus::kUnsupported;
    result.diagnostics.push_back("first-fit does not support flexible pool assignment");
    return result;
  }

  std::vector<std::size_t> order = SortedSuperOrder(problem, prepared, priority);
  std::stable_partition(order.begin(), order.end(), [&](std::size_t index) {
    return prepared.supers[index].pinned.has_value();
  });

  std::vector<std::size_t> placed;
  DsaSolution solution;
  for (std::size_t index : order) {
    SuperBuffer& current = prepared.supers[index];
    const PoolId pool_id = current.pinned ? current.pinned->pool : current.allowed_pools.front();
    const Pool* pool = problem.FindPool(pool_id);
    if (pool == nullptr) {
      result.status = SolveStatus::kInvalidProblem;
      result.diagnostics.push_back("placement references an unknown pool");
      return result;
    }

    if (current.pinned) {
      current.offset = current.pinned->offset;
      if (AddOverflows(current.offset, current.size)) {
        result.status = SolveStatus::kInvalidProblem;
        result.diagnostics.push_back("pinned colocation class address range overflows uint64");
        return result;
      }
    } else {
      std::vector<AddressRange> blocked = pool->reserved_ranges;
      for (std::size_t other_index : placed) {
        const SuperBuffer& other = prepared.supers[other_index];
        if (other.placed_pool != pool_id) continue;
        if (SuperBuffersConflict(problem, current, other) ||
            Separated(prepared, index, other_index) || current.pinned_exclusive ||
            other.pinned_exclusive) {
          blocked.push_back({other.offset, other.offset + other.size});
        }
      }
      const std::optional<std::uint64_t> offset =
          FindFirstFit({current.size, current.alignment}, std::move(blocked));
      if (!offset) {
        result.status = SolveStatus::kBestEffortNoFit;
        result.diagnostics.push_back("address arithmetic overflow while placing buffer class");
        return result;
      }
      current.offset = *offset;
    }
    current.placed_pool = pool_id;
    placed.push_back(index);
    for (std::size_t member : current.members) {
      solution.placements[problem.buffers[member].id] = {pool_id, current.offset};
    }
  }

  DsaProblem without_caps = problem;
  for (Pool& pool : without_caps.pools) pool.capacity.reset();
  const std::vector<std::string> structural_errors = ValidateSolution(without_caps, solution);
  if (!structural_errors.empty()) {
    result.status = SolveStatus::kInfeasibleProven;
    result.solution = std::move(solution);
    result.objective = EvaluateObjective(problem, *result.solution);
    result.diagnostics = structural_errors;
    return result;
  }

  result.objective = EvaluateObjective(problem, solution);
  bool over_capacity = false;
  for (const Pool& pool : problem.pools) {
    const auto peak = result.objective.peak_by_pool.find(pool.id);
    if (pool.capacity && peak != result.objective.peak_by_pool.end() &&
        peak->second > *pool.capacity) {
      over_capacity = true;
      result.diagnostics.push_back("pool " + std::to_string(pool.id) + " peak " +
                                   std::to_string(peak->second) + " exceeds capacity " +
                                   std::to_string(*pool.capacity));
    }
  }
  result.status = over_capacity ? SolveStatus::kBestEffortNoFit : SolveStatus::kFeasible;
  result.solution = std::move(solution);
  return result;
}

}  // namespace dsa::detail

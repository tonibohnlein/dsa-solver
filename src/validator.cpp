#include "dsa/validator.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dsa {
namespace {

bool AddOverflows(std::uint64_t first, std::uint64_t second) {
  return first > std::numeric_limits<std::uint64_t>::max() - second;
}

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

std::unordered_map<BufferId, std::size_t> BufferIndices(const DsaProblem& problem) {
  std::unordered_map<BufferId, std::size_t> result;
  for (std::size_t i = 0; i < problem.buffers.size(); ++i) {
    result.emplace(problem.buffers[i].id, i);
  }
  return result;
}

bool PoolAllowed(const Buffer& buffer, PoolId pool) {
  return std::find(buffer.allowed_pools.begin(), buffer.allowed_pools.end(), pool) !=
         buffer.allowed_pools.end();
}

std::string PairName(BufferId first, BufferId second) {
  return std::to_string(first) + "," + std::to_string(second);
}

}  // namespace

bool LifetimesOverlap(const Buffer& first, const Buffer& second) noexcept {
  for (const Interval& first_interval : first.live_intervals) {
    for (const Interval& second_interval : second.live_intervals) {
      if (first_interval.Overlaps(second_interval)) return true;
    }
  }
  return false;
}

bool HaveTemporalConflict(const DsaProblem& problem, const Buffer& first,
                          const Buffer& second) noexcept {
  for (const TemporalExclusion& exclusion : problem.temporal_exclusions) {
    if ((exclusion.first == first.id && exclusion.second == second.id) ||
        (exclusion.first == second.id && exclusion.second == first.id)) {
      return false;
    }
  }
  return LifetimesOverlap(first, second);
}

bool AddressRangesOverlap(std::uint64_t first_offset, std::uint64_t first_size,
                          std::uint64_t second_offset, std::uint64_t second_size) noexcept {
  if (first_size == 0 || second_size == 0) return false;
  if (AddOverflows(first_offset, first_size) || AddOverflows(second_offset, second_size)) {
    return true;
  }
  return first_offset < second_offset + second_size && second_offset < first_offset + first_size;
}

std::vector<std::string> ValidateProblem(const DsaProblem& problem) {
  std::vector<std::string> errors;
  std::set<PoolId> pool_ids;
  for (const Pool& pool : problem.pools) {
    if (!pool_ids.insert(pool.id).second) {
      errors.push_back("duplicate pool id " + std::to_string(pool.id));
    }
    for (const AddressRange& range : pool.reserved_ranges) {
      if (range.begin >= range.end) {
        errors.push_back("pool " + std::to_string(pool.id) +
                         " has an empty or inverted reserved range");
      }
      if (pool.capacity && range.end > *pool.capacity) {
        errors.push_back("pool " + std::to_string(pool.id) + " reserved range exceeds capacity");
      }
    }
    if (pool.bank_geometry &&
        (pool.bank_geometry->bank_size == 0 || pool.bank_geometry->num_banks == 0)) {
      errors.push_back("pool " + std::to_string(pool.id) + " has invalid bank geometry");
    }
  }
  if (problem.pools.empty()) errors.push_back("problem has no pools");

  std::set<BufferId> buffer_ids;
  std::set<std::string> buffer_names;
  for (const Buffer& buffer : problem.buffers) {
    if (!buffer_ids.insert(buffer.id).second) {
      errors.push_back("duplicate buffer id " + std::to_string(buffer.id));
    }
    if (!buffer.name.empty() && !buffer_names.insert(buffer.name).second) {
      errors.push_back("duplicate buffer name '" + buffer.name + "'");
    }
    if (buffer.size == 0) {
      errors.push_back("buffer " + std::to_string(buffer.id) + " has zero size");
    }
    if (buffer.alignment == 0) {
      errors.push_back("buffer " + std::to_string(buffer.id) + " alignment is zero");
    }
    if (buffer.live_intervals.empty()) {
      errors.push_back("buffer " + std::to_string(buffer.id) + " has no lifetime intervals");
    }
    for (const Interval& interval : buffer.live_intervals) {
      if (interval.lower >= interval.upper) {
        errors.push_back("buffer " + std::to_string(buffer.id) +
                         " has an empty or inverted lifetime interval");
      }
    }
    if (buffer.allowed_pools.empty()) {
      errors.push_back("buffer " + std::to_string(buffer.id) + " has no allowed pools");
    }
    std::set<PoolId> allowed;
    for (PoolId pool : buffer.allowed_pools) {
      if (!allowed.insert(pool).second) {
        errors.push_back("buffer " + std::to_string(buffer.id) + " repeats an allowed pool");
      }
      if (pool_ids.count(pool) == 0) {
        errors.push_back("buffer " + std::to_string(buffer.id) + " references unknown pool " +
                         std::to_string(pool));
      }
    }
  }

  std::set<std::pair<BufferId, BufferId>> colocated;
  for (const Colocation& constraint : problem.colocations) {
    if (buffer_ids.count(constraint.first) == 0 || buffer_ids.count(constraint.second) == 0) {
      errors.push_back("colocation references an unknown buffer: " +
                       PairName(constraint.first, constraint.second));
      continue;
    }
    colocated.emplace(std::min(constraint.first, constraint.second),
                      std::max(constraint.first, constraint.second));
  }
  for (const Separation& constraint : problem.separations) {
    if (buffer_ids.count(constraint.first) == 0 || buffer_ids.count(constraint.second) == 0) {
      errors.push_back("separation references an unknown buffer: " +
                       PairName(constraint.first, constraint.second));
      continue;
    }
    const auto key = std::make_pair(std::min(constraint.first, constraint.second),
                                    std::max(constraint.first, constraint.second));
    if (constraint.first == constraint.second || colocated.count(key) != 0) {
      errors.push_back("separation contradicts colocation for buffers " +
                       PairName(constraint.first, constraint.second));
    }
    std::set<SeparationReason> reasons;
    for (SeparationReason reason : constraint.reasons) {
      if (!reasons.insert(reason).second) {
        errors.push_back("separation repeats reason '" + std::string(ToString(reason)) +
                         "' for buffers " + PairName(constraint.first, constraint.second));
      }
    }
  }
  for (const TemporalExclusion& exclusion : problem.temporal_exclusions) {
    if (buffer_ids.count(exclusion.first) == 0 || buffer_ids.count(exclusion.second) == 0) {
      errors.push_back("temporal exclusion references an unknown buffer: " +
                       PairName(exclusion.first, exclusion.second));
    } else if (exclusion.first == exclusion.second) {
      errors.push_back("temporal exclusion references the same buffer twice");
    }
  }

  std::set<BufferId> pinned_ids;
  for (const PinnedAllocation& pinned : problem.pinned_allocations) {
    const Buffer* buffer = problem.FindBuffer(pinned.buffer);
    if (buffer == nullptr) {
      errors.push_back("pinned allocation references unknown buffer " +
                       std::to_string(pinned.buffer));
      continue;
    }
    if (!pinned_ids.insert(pinned.buffer).second) {
      errors.push_back("buffer " + std::to_string(pinned.buffer) + " is pinned more than once");
    }
    if (problem.FindPool(pinned.pool) == nullptr || !PoolAllowed(*buffer, pinned.pool)) {
      errors.push_back("buffer " + std::to_string(pinned.buffer) +
                       " is pinned to a disallowed pool");
    }
    if (pinned.offset % buffer->alignment != 0) {
      errors.push_back("buffer " + std::to_string(pinned.buffer) +
                       " has a misaligned pinned offset");
    }
    if (AddOverflows(pinned.offset, buffer->size)) {
      errors.push_back("buffer " + std::to_string(pinned.buffer) +
                       " pinned address range overflows uint64");
    } else if (const Pool* pool = problem.FindPool(pinned.pool)) {
      if (pool->capacity && pinned.offset + buffer->size > *pool->capacity) {
        errors.push_back("buffer " + std::to_string(pinned.buffer) +
                         " pinned address exceeds pool capacity");
      }
      for (const AddressRange& reserved : pool->reserved_ranges) {
        if (AddressRangesOverlap(pinned.offset, buffer->size, reserved.begin,
                                 reserved.end - reserved.begin)) {
          errors.push_back("buffer " + std::to_string(pinned.buffer) +
                           " pinned address overlaps a reserved range");
        }
      }
    }
  }

  if (problem.cost_model) {
    for (const ReusePenalty& penalty : problem.cost_model->reuse_penalties) {
      if (buffer_ids.count(penalty.first) == 0 || buffer_ids.count(penalty.second) == 0) {
        errors.push_back("reuse penalty references an unknown buffer: " +
                         PairName(penalty.first, penalty.second));
      }
    }
  }

  if (problem.pypto_structure) {
    std::set<BufferId> alias_buffers;
    for (const PyptoAliasClass& alias_class : problem.pypto_structure->alias_classes) {
      if (buffer_ids.count(alias_class.buffer) == 0) {
        errors.push_back("PyPTO alias class references unknown buffer " +
                         std::to_string(alias_class.buffer));
      }
      if (!alias_buffers.insert(alias_class.buffer).second) {
        errors.push_back("PyPTO alias structure repeats buffer " +
                         std::to_string(alias_class.buffer));
      }
      if (alias_class.members.empty()) {
        errors.push_back("PyPTO alias class for buffer " + std::to_string(alias_class.buffer) +
                         " has no members");
      }
    }

    std::set<std::pair<PoolId, std::int32_t>> pipeline_group_ids;
    for (const PyptoPipelineGroup& group : problem.pypto_structure->pipeline_groups) {
      const std::string group_name =
          std::to_string(group.group) + " in pool " + std::to_string(group.pool);
      if (group.group < 0) errors.push_back("PyPTO pipeline group id must be non-negative");
      if (!pipeline_group_ids.emplace(group.pool, group.group).second) {
        errors.push_back("duplicate PyPTO pipeline group " + group_name);
      }
      if (problem.FindPool(group.pool) == nullptr) {
        errors.push_back("PyPTO pipeline group " + group_name + " references an unknown pool");
      }
      if (group.slot_size == 0) {
        errors.push_back("PyPTO pipeline group " + group_name + " has zero slot size");
      }
      if (group.depth == 0) {
        errors.push_back("PyPTO pipeline group " + group_name + " has zero depth");
      }
      if (group.effective_depth == 0 || group.effective_depth > group.depth) {
        errors.push_back("PyPTO pipeline group " + group_name + " has invalid effective depth");
      }
      if (group.members.empty()) {
        errors.push_back("PyPTO pipeline group " + group_name + " has no members");
      }

      std::set<std::pair<BufferId, std::int32_t>> pipeline_members;
      std::map<std::int32_t, std::uint32_t> residue_by_stage;
      for (const PyptoPipelineMember& member : group.members) {
        const Buffer* buffer = problem.FindBuffer(member.buffer);
        if (buffer == nullptr) {
          errors.push_back("PyPTO pipeline group " + group_name + " references unknown buffer " +
                           std::to_string(member.buffer));
        } else {
          if (!PoolAllowed(*buffer, group.pool)) {
            errors.push_back("PyPTO pipeline member " + std::to_string(member.buffer) +
                             " does not allow group pool " + std::to_string(group.pool));
          }
          if (buffer->size > group.slot_size) {
            errors.push_back("PyPTO pipeline member " + std::to_string(member.buffer) +
                             " exceeds group slot size");
          }
        }
        if (member.stage < 0) {
          errors.push_back("PyPTO pipeline member has a negative stage");
        }
        if (member.residue >= group.effective_depth) {
          errors.push_back("PyPTO pipeline member residue exceeds effective depth");
        }
        if (!pipeline_members.emplace(member.buffer, member.stage).second) {
          errors.push_back("PyPTO pipeline group " + group_name + " repeats a buffer/stage member");
        }
        const auto [stage, inserted] = residue_by_stage.emplace(member.stage, member.residue);
        if (!inserted && stage->second != member.residue) {
          errors.push_back("PyPTO pipeline stage maps to multiple residues");
        }
      }
      if (residue_by_stage.size() != group.depth) {
        errors.push_back("PyPTO pipeline group " + group_name +
                         " depth does not match its distinct stages");
      }
    }
  }
  if (problem.objective.terms.empty()) {
    errors.push_back("objective has no terms");
  }
  std::set<ObjectiveMetric> objective_terms;
  for (ObjectiveMetric metric : problem.objective.terms) {
    if (!objective_terms.insert(metric).second) {
      errors.push_back(std::string("objective repeats metric '") + ToString(metric) + "'");
    }
  }
  return errors;
}

std::vector<std::string> ValidateSolution(const DsaProblem& problem, const DsaSolution& solution) {
  std::vector<std::string> errors = ValidateProblem(problem);
  if (!errors.empty()) return errors;

  const auto indices = BufferIndices(problem);
  DisjointSet colocations(problem.buffers.size());
  for (const Colocation& constraint : problem.colocations) {
    colocations.Union(indices.at(constraint.first), indices.at(constraint.second));
  }
  std::set<BufferId> exclusive_pins;
  for (const PinnedAllocation& pinned : problem.pinned_allocations) {
    if (pinned.exclusive_for_all_time) exclusive_pins.insert(pinned.buffer);
  }

  for (const auto& [id, placement] : solution.placements) {
    if (problem.FindBuffer(id) == nullptr) {
      errors.push_back("solution contains unknown buffer " + std::to_string(id));
    }
    if (problem.FindPool(placement.pool) == nullptr) {
      errors.push_back("solution references unknown pool " + std::to_string(placement.pool));
    }
  }

  for (const Buffer& buffer : problem.buffers) {
    const Placement* placement = solution.Find(buffer.id);
    if (placement == nullptr) {
      errors.push_back("buffer " + std::to_string(buffer.id) + " has no placement");
      continue;
    }
    if (!PoolAllowed(buffer, placement->pool)) {
      errors.push_back("buffer " + std::to_string(buffer.id) + " is placed in a disallowed pool");
    }
    if (placement->offset % buffer.alignment != 0) {
      errors.push_back("buffer " + std::to_string(buffer.id) + " violates alignment");
    }
    if (AddOverflows(placement->offset, buffer.size)) {
      errors.push_back("buffer " + std::to_string(buffer.id) + " address range overflows uint64");
      continue;
    }
    const Pool* pool = problem.FindPool(placement->pool);
    if (pool == nullptr) continue;
    if (pool->capacity && placement->offset + buffer.size > *pool->capacity) {
      errors.push_back("buffer " + std::to_string(buffer.id) + " exceeds pool capacity");
    }
    for (const AddressRange& reserved : pool->reserved_ranges) {
      if (AddressRangesOverlap(placement->offset, buffer.size, reserved.begin,
                               reserved.end - reserved.begin)) {
        errors.push_back("buffer " + std::to_string(buffer.id) + " overlaps a reserved range");
      }
    }
  }

  for (const Colocation& constraint : problem.colocations) {
    const Placement* first = solution.Find(constraint.first);
    const Placement* second = solution.Find(constraint.second);
    if (first && second && (first->pool != second->pool || first->offset != second->offset)) {
      errors.push_back("colocated buffers " + PairName(constraint.first, constraint.second) +
                       " do not share a placement");
    }
  }

  for (std::size_t i = 0; i < problem.buffers.size(); ++i) {
    for (std::size_t j = i + 1; j < problem.buffers.size(); ++j) {
      if (colocations.Find(i) == colocations.Find(j)) continue;
      const Buffer& first_buffer = problem.buffers[i];
      const Buffer& second_buffer = problem.buffers[j];
      const bool must_not_reuse = HaveTemporalConflict(problem, first_buffer, second_buffer) ||
                                  exclusive_pins.count(first_buffer.id) != 0 ||
                                  exclusive_pins.count(second_buffer.id) != 0;
      if (!must_not_reuse) continue;
      const Placement* first = solution.Find(first_buffer.id);
      const Placement* second = solution.Find(second_buffer.id);
      if (first && second && first->pool == second->pool &&
          AddressRangesOverlap(first->offset, first_buffer.size, second->offset,
                               second_buffer.size)) {
        errors.push_back("lifetime-overlapping buffers " +
                         PairName(first_buffer.id, second_buffer.id) + " overlap in address");
      }
    }
  }

  // PyPTO's downstream dependency tracking models reuse through a shared
  // allocation base.  It does not currently make arbitrary partially
  // overlapping address ranges safe.  Preserve the standard DSA semantics for
  // ordinary problems, but reject such placements for the structured compiler
  // profile. Equal offsets are whole-slot reuse and remain legal subject to the
  // lifetime/separation checks above and below.
  if (problem.pypto_structure && problem.pypto_structure->whole_slot_reuse) {
    for (std::size_t i = 0; i < problem.buffers.size(); ++i) {
      for (std::size_t j = i + 1; j < problem.buffers.size(); ++j) {
        const Buffer& first_buffer = problem.buffers[i];
        const Buffer& second_buffer = problem.buffers[j];
        const Placement* first = solution.Find(first_buffer.id);
        const Placement* second = solution.Find(second_buffer.id);
        if (first && second && first->pool == second->pool && first->offset != second->offset &&
            AddressRangesOverlap(first->offset, first_buffer.size, second->offset,
                                 second_buffer.size)) {
          errors.push_back("PyPTO whole-slot buffers " +
                           PairName(first_buffer.id, second_buffer.id) +
                           " partially overlap in address");
        }
      }
    }
  }

  for (const Separation& constraint : problem.separations) {
    const Buffer* first_buffer = problem.FindBuffer(constraint.first);
    const Buffer* second_buffer = problem.FindBuffer(constraint.second);
    const Placement* first = solution.Find(constraint.first);
    const Placement* second = solution.Find(constraint.second);
    if (first_buffer && second_buffer && first && second && first->pool == second->pool &&
        AddressRangesOverlap(first->offset, first_buffer->size, second->offset,
                             second_buffer->size)) {
      errors.push_back("separated buffers " + PairName(constraint.first, constraint.second) +
                       " overlap in address");
    }
  }

  for (const PinnedAllocation& pinned : problem.pinned_allocations) {
    const Placement* placement = solution.Find(pinned.buffer);
    if (placement && (placement->pool != pinned.pool || placement->offset != pinned.offset)) {
      errors.push_back("buffer " + std::to_string(pinned.buffer) +
                       " violates its pinned placement");
    }
  }
  return errors;
}

ObjectiveValue EvaluateObjective(const DsaProblem& problem, const DsaSolution& solution) {
  ObjectiveValue objective;
  for (const Pool& pool : problem.pools) {
    for (const AddressRange& reserved : pool.reserved_ranges) {
      std::uint64_t& peak = objective.peak_by_pool[pool.id];
      peak = std::max(peak, reserved.end);
    }
  }
  for (const Buffer& buffer : problem.buffers) {
    const Placement* placement = solution.Find(buffer.id);
    if (placement == nullptr || AddOverflows(placement->offset, buffer.size)) continue;
    std::uint64_t& peak = objective.peak_by_pool[placement->pool];
    peak = std::max(peak, placement->offset + buffer.size);
  }
  for (const auto& [pool, peak] : objective.peak_by_pool) {
    static_cast<void>(pool);
    objective.total_peak += peak;
    objective.max_peak = std::max(objective.max_peak, peak);
  }

  if (problem.cost_model) {
    const auto indices = BufferIndices(problem);
    DisjointSet colocations(problem.buffers.size());
    for (const Colocation& constraint : problem.colocations) {
      const auto first = indices.find(constraint.first);
      const auto second = indices.find(constraint.second);
      if (first != indices.end() && second != indices.end()) {
        colocations.Union(first->second, second->second);
      }
    }
    for (const ReusePenalty& penalty : problem.cost_model->reuse_penalties) {
      const Buffer* first_buffer = problem.FindBuffer(penalty.first);
      const Buffer* second_buffer = problem.FindBuffer(penalty.second);
      const Placement* first = solution.Find(penalty.first);
      const Placement* second = solution.Find(penalty.second);
      const auto first_index = indices.find(penalty.first);
      const auto second_index = indices.find(penalty.second);
      const bool intentionally_colocated =
          first_index != indices.end() && second_index != indices.end() &&
          colocations.Find(first_index->second) == colocations.Find(second_index->second);
      if (first_buffer && second_buffer && first && second && first->pool == second->pool &&
          !intentionally_colocated &&
          !HaveTemporalConflict(problem, *first_buffer, *second_buffer) &&
          AddressRangesOverlap(first->offset, first_buffer->size, second->offset,
                               second_buffer->size)) {
        objective.reuse_cost += penalty.cost;
      }
    }
  }
  return objective;
}

std::uint64_t EvaluateObjectiveMetric(const DsaProblem& problem, const ObjectiveValue& objective,
                                      ObjectiveMetric metric) noexcept {
  switch (metric) {
    case ObjectiveMetric::kCapacityOverflow: {
      std::uint64_t overflow = 0;
      for (const Pool& pool : problem.pools) {
        if (!pool.capacity) continue;
        const auto found = objective.peak_by_pool.find(pool.id);
        const std::uint64_t peak = found == objective.peak_by_pool.end() ? 0 : found->second;
        if (peak <= *pool.capacity) continue;
        const std::uint64_t amount = peak - *pool.capacity;
        overflow = AddOverflows(overflow, amount) ? std::numeric_limits<std::uint64_t>::max()
                                                  : overflow + amount;
      }
      return overflow;
    }
    case ObjectiveMetric::kTotalPeak:
      return objective.total_peak;
    case ObjectiveMetric::kMaxPeak:
      return objective.max_peak;
    case ObjectiveMetric::kReuseCost:
      return objective.reuse_cost;
    case ObjectiveMetric::kBankCost:
      return objective.bank_cost;
  }
  return std::numeric_limits<std::uint64_t>::max();
}

}  // namespace dsa

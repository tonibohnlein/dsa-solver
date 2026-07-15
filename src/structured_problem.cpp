#include "dsa/structured_problem.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dsa/validator.h"

namespace dsa {
namespace {

bool UsesOnlyStandardObjectiveMetrics(const ObjectiveSpec& objective) {
  return std::all_of(objective.terms.begin(), objective.terms.end(), [](ObjectiveMetric metric) {
    return metric == ObjectiveMetric::kCapacityOverflow || metric == ObjectiveMetric::kTotalPeak ||
           metric == ObjectiveMetric::kMaxPeak;
  });
}

std::vector<std::string> ValidateStandardProblem(const DsaProblem& problem) {
  std::vector<std::string> errors;
  if (problem.pools.size() != 1) {
    errors.push_back("standard DSA profile requires exactly one pool");
    return errors;
  }
  const Pool& pool = problem.pools.front();
  if (!pool.reserved_ranges.empty()) {
    errors.push_back("standard DSA profile cannot encode reserved ranges");
  }
  if (pool.bank_geometry) {
    errors.push_back("standard DSA profile cannot encode bank geometry");
  }
  for (const Buffer& buffer : problem.buffers) {
    if (buffer.live_intervals.size() != 1) {
      errors.push_back("standard DSA buffer " + std::to_string(buffer.id) +
                       " must have exactly one live interval");
    }
    if (buffer.alignment != 1) {
      errors.push_back("standard DSA buffer " + std::to_string(buffer.id) +
                       " must have alignment 1");
    }
    if (buffer.allowed_pools.size() != 1 || buffer.allowed_pools.front() != pool.id) {
      errors.push_back("standard DSA buffer " + std::to_string(buffer.id) +
                       " must use the document's fixed pool");
    }
  }
  if (!problem.colocations.empty()) {
    errors.push_back("standard DSA profile cannot encode colocations");
  }
  if (!problem.separations.empty()) {
    errors.push_back("standard DSA profile cannot encode separations");
  }
  if (!problem.temporal_exclusions.empty()) {
    errors.push_back("standard DSA profile cannot encode temporal exclusions");
  }
  if (!problem.pinned_allocations.empty()) {
    errors.push_back("standard DSA profile cannot encode pinned allocations");
  }
  if (problem.cost_model) {
    errors.push_back("standard DSA profile cannot encode a cost model");
  }
  if (problem.pypto_structure) {
    errors.push_back("standard DSA profile cannot encode PyPTO structure");
  }
  if (!UsesOnlyStandardObjectiveMetrics(problem.objective)) {
    errors.push_back("standard DSA profile uses a structured objective metric");
  }
  return errors;
}

bool ObjectiveIsMinimizePeak(const ObjectiveSpec& objective) {
  const ObjectiveSpec expected = MinimizePeakObjective();
  return objective.aggregation == expected.aggregation && objective.terms == expected.terms;
}

void RequireMetadataValue(const StructuredProblemDocument& document, const std::string& key,
                          const std::string& expected, std::vector<std::string>* errors) {
  const auto found = document.metadata.find(key);
  if (found == document.metadata.end() || found->second != expected) {
    errors->push_back(std::string(ToString(document.profile)) + " requires metadata '" + key +
                      "' = '" + expected + "'");
  }
}

std::vector<std::string> ValidatePyptoV1(const StructuredProblemDocument& document,
                                         bool reject_experimental) {
  const DsaProblem& problem = document.problem;
  const std::string profile = ToString(document.profile);
  std::vector<std::string> errors;
  RequireMetadataValue(document, "lifetime_ordering", "pypto_read_before_write", &errors);
  RequireMetadataValue(document, "solver_input", "pre_memory_reuse", &errors);
  RequireMetadataValue(document, "address_reuse_contract", "whole_slot_v1", &errors);
  if (!problem.pypto_structure || !problem.pypto_structure->whole_slot_reuse) {
    errors.push_back(profile + " requires whole-slot address reuse");
  }
  if (!reject_experimental) return errors;
  for (const Buffer& buffer : problem.buffers) {
    if (buffer.live_intervals.size() != 1) {
      errors.push_back("pypto_hard_v1 buffer " + std::to_string(buffer.id) +
                       " must have one conservative live interval");
    }
    if (buffer.allowed_pools.size() != 1) {
      errors.push_back("pypto_hard_v1 buffer " + std::to_string(buffer.id) +
                       " must have one fixed pool");
    }
  }
  for (const Pool& pool : problem.pools) {
    if (pool.bank_geometry) {
      errors.push_back("pypto_hard_v1 cannot encode experimental bank geometry");
      break;
    }
  }
  if (!problem.colocations.empty()) {
    errors.push_back("pypto_hard_v1 cannot encode experimental colocations");
  }
  if (!problem.temporal_exclusions.empty()) {
    errors.push_back("pypto_hard_v1 cannot encode experimental temporal exclusions");
  }
  if (!problem.pinned_allocations.empty()) {
    errors.push_back("pypto_hard_v1 cannot encode experimental pinned allocations");
  }
  if (problem.cost_model) {
    errors.push_back("pypto_hard_v1 cannot encode an experimental cost model");
  }
  if (!ObjectiveIsMinimizePeak(problem.objective)) {
    errors.push_back("pypto_hard_v1 requires the peak-minimization objective");
  }
  return errors;
}

bool PairTouches(const std::unordered_set<BufferId>& selected, BufferId first, BufferId second) {
  return selected.count(first) != 0 || selected.count(second) != 0;
}

}  // namespace

const char* ToString(BenchmarkProfile profile) noexcept {
  switch (profile) {
    case BenchmarkProfile::kStandardDsa:
      return "standard_dsa";
    case BenchmarkProfile::kPyptoStructured:
      return "pypto_structured";
    case BenchmarkProfile::kPyptoHardV1:
      return "pypto_hard_v1";
    case BenchmarkProfile::kPyptoResearchV1:
      return "pypto_research_v1";
    case BenchmarkProfile::kPyptoCoreRelaxation:
      return "pypto_core_relaxation";
  }
  return "unknown";
}

bool IsPyptoProfile(BenchmarkProfile profile) noexcept {
  return profile == BenchmarkProfile::kPyptoStructured ||
         profile == BenchmarkProfile::kPyptoHardV1 || profile == BenchmarkProfile::kPyptoResearchV1;
}

std::vector<std::string> ValidateStructuredProblemDocument(
    const StructuredProblemDocument& document) {
  std::vector<std::string> errors;
  if (document.schema_version != kStructuredProblemSchemaVersion) {
    errors.push_back("unsupported structured problem schema version " +
                     std::to_string(document.schema_version));
  }
  if (document.instance.empty()) errors.push_back("structured problem has an empty instance name");

  std::vector<std::string> problem_errors = ValidateProblem(document.problem);
  errors.insert(errors.end(), problem_errors.begin(), problem_errors.end());

  switch (document.profile) {
    case BenchmarkProfile::kStandardDsa:
      if (document.relaxed_from) {
        errors.push_back("standard DSA document cannot declare relaxed_from");
      }
      if (!document.relaxed_features.empty()) {
        errors.push_back("standard DSA document cannot declare relaxed_features");
      }
      break;
    case BenchmarkProfile::kPyptoStructured:
      if (document.relaxed_from) {
        errors.push_back("legacy structured PyPTO document cannot declare relaxed_from");
      }
      if (!document.relaxed_features.empty()) {
        errors.push_back("legacy structured PyPTO document cannot declare relaxed_features");
      }
      break;
    case BenchmarkProfile::kPyptoHardV1:
      if (document.relaxed_from) {
        errors.push_back("pypto_hard_v1 document cannot declare relaxed_from");
      }
      if (!document.relaxed_features.empty()) {
        errors.push_back("pypto_hard_v1 document cannot declare relaxed_features");
      }
      {
        std::vector<std::string> hard_errors = ValidatePyptoV1(document, true);
        errors.insert(errors.end(), hard_errors.begin(), hard_errors.end());
      }
      break;
    case BenchmarkProfile::kPyptoResearchV1:
      if (document.relaxed_from) {
        errors.push_back("pypto_research_v1 document cannot declare relaxed_from");
      }
      if (!document.relaxed_features.empty()) {
        errors.push_back("pypto_research_v1 document cannot declare relaxed_features");
      }
      {
        std::vector<std::string> research_errors = ValidatePyptoV1(document, false);
        errors.insert(errors.end(), research_errors.begin(), research_errors.end());
      }
      break;
    case BenchmarkProfile::kPyptoCoreRelaxation:
      if (!document.relaxed_from || document.relaxed_from->empty()) {
        errors.push_back("core relaxation must identify relaxed_from");
      }
      break;
  }

  if (document.profile == BenchmarkProfile::kStandardDsa ||
      document.profile == BenchmarkProfile::kPyptoCoreRelaxation) {
    std::vector<std::string> standard_errors = ValidateStandardProblem(document.problem);
    errors.insert(errors.end(), standard_errors.begin(), standard_errors.end());
  }
  return errors;
}

std::vector<StructuredProblemDocument> BuildCoreRelaxations(
    const StructuredProblemDocument& source) {
  const std::vector<std::string> source_errors = ValidateStructuredProblemDocument(source);
  if (!source_errors.empty()) {
    throw std::invalid_argument("cannot relax an invalid structured problem: " +
                                source_errors.front());
  }
  if (!IsPyptoProfile(source.profile)) {
    throw std::invalid_argument("core relaxations require a PyPTO source document");
  }
  if (!source.problem.temporal_exclusions.empty()) {
    throw std::invalid_argument(
        "schema v1 cannot soundly project temporal exclusions to interval-only standard DSA");
  }
  if (!source.problem.colocations.empty()) {
    throw std::invalid_argument(
        "schema v1 cannot soundly project colocations to independent standard DSA rows");
  }
  for (const Buffer& buffer : source.problem.buffers) {
    if (buffer.allowed_pools.size() != 1) {
      throw std::invalid_argument(
          "schema v1 cannot soundly project flexible pool assignment to standard DSA");
    }
    for (std::size_t first = 0; first < buffer.live_intervals.size(); ++first) {
      for (std::size_t second = first + 1; second < buffer.live_intervals.size(); ++second) {
        if (buffer.live_intervals[first].Overlaps(buffer.live_intervals[second])) {
          throw std::invalid_argument("schema v1 cannot split overlapping intervals of buffer " +
                                      std::to_string(buffer.id) +
                                      " into independent standard DSA rows");
        }
      }
    }
  }

  std::vector<StructuredProblemDocument> documents;
  for (const Pool& source_pool : source.problem.pools) {
    std::vector<const Buffer*> selected_buffers;
    std::unordered_set<BufferId> selected_ids;
    for (const Buffer& buffer : source.problem.buffers) {
      if (buffer.allowed_pools.front() == source_pool.id) {
        selected_buffers.push_back(&buffer);
        selected_ids.insert(buffer.id);
      }
    }
    if (selected_buffers.empty()) continue;

    StructuredProblemDocument relaxed;
    relaxed.profile = BenchmarkProfile::kPyptoCoreRelaxation;
    relaxed.instance = source.instance + "::pool=" + std::to_string(source_pool.id);
    relaxed.relaxed_from = source.instance;
    relaxed.metadata = source.metadata;
    relaxed.metadata["source_profile"] = ToString(source.profile);
    relaxed.metadata["source_pool_id"] = std::to_string(source_pool.id);
    relaxed.metadata["source_pool_name"] = source_pool.name;
    relaxed.metadata["sound_lower_bound"] = "true";
    relaxed.problem.pools = {
        {kDefaultPool, source_pool.name, source_pool.capacity, {}, std::nullopt}};
    relaxed.problem.objective = MinimizePeakObjective();

    std::set<std::string> relaxed_features;
    if (source.problem.pools.size() > 1) relaxed_features.insert("multi_pool_partition");
    if (!source_pool.reserved_ranges.empty()) relaxed_features.insert("reserved_ranges");
    if (source_pool.bank_geometry) relaxed_features.insert("bank_geometry");
    if (!ObjectiveIsMinimizePeak(source.problem.objective)) {
      relaxed_features.insert("structured_objective");
    }
    if (source.problem.pypto_structure) relaxed_features.insert("pypto_structure");

    std::uint64_t next_id = 0;
    for (const Buffer* source_buffer : selected_buffers) {
      if (source_buffer->alignment != 1) relaxed_features.insert("alignment");
      if (source_buffer->live_intervals.size() > 1) {
        relaxed_features.insert("multi_interval_identity");
      }
      for (std::size_t interval_index = 0; interval_index < source_buffer->live_intervals.size();
           ++interval_index) {
        if (next_id > std::numeric_limits<BufferId>::max()) {
          throw std::overflow_error("core relaxation has too many interval fragments");
        }
        Buffer buffer;
        buffer.id = static_cast<BufferId>(next_id++);
        const std::string base_name =
            "buffer" + std::to_string(source_buffer->id) +
            (source_buffer->name.empty() ? "" : ":" + source_buffer->name);
        buffer.name = source_buffer->live_intervals.size() == 1
                          ? base_name
                          : base_name + "@interval" + std::to_string(interval_index);
        buffer.size = source_buffer->size;
        buffer.alignment = 1;
        buffer.live_intervals = {source_buffer->live_intervals[interval_index]};
        buffer.allowed_pools = {kDefaultPool};
        relaxed.problem.buffers.push_back(std::move(buffer));
      }
    }

    if (std::any_of(source.problem.colocations.begin(), source.problem.colocations.end(),
                    [&](const Colocation& value) {
                      return PairTouches(selected_ids, value.first, value.second);
                    })) {
      relaxed_features.insert("colocations");
    }
    if (std::any_of(source.problem.separations.begin(), source.problem.separations.end(),
                    [&](const Separation& value) {
                      return PairTouches(selected_ids, value.first, value.second);
                    })) {
      relaxed_features.insert("separations");
    }
    if (std::any_of(
            source.problem.pinned_allocations.begin(), source.problem.pinned_allocations.end(),
            [&](const PinnedAllocation& value) { return selected_ids.count(value.buffer) != 0; })) {
      relaxed_features.insert("pinned_allocations");
    }
    if (source.problem.cost_model && std::any_of(source.problem.cost_model->reuse_penalties.begin(),
                                                 source.problem.cost_model->reuse_penalties.end(),
                                                 [&](const ReusePenalty& value) {
                                                   return PairTouches(selected_ids, value.first,
                                                                      value.second);
                                                 })) {
      relaxed_features.insert("reuse_penalties");
    }
    relaxed.relaxed_features.assign(relaxed_features.begin(), relaxed_features.end());

    const std::vector<std::string> relaxed_errors = ValidateStructuredProblemDocument(relaxed);
    if (!relaxed_errors.empty()) {
      throw std::logic_error("generated an invalid core relaxation: " + relaxed_errors.front());
    }
    documents.push_back(std::move(relaxed));
  }
  return documents;
}

}  // namespace dsa

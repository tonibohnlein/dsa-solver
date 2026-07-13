#include "dsa/solver.h"

#include <algorithm>
#include <string>
#include <utility>

namespace dsa {
namespace {

void AddUnique(std::vector<std::string>* values, std::string value) {
  if (std::find(values->begin(), values->end(), value) == values->end()) {
    values->push_back(std::move(value));
  }
}

void RequireFeature(SolverCompatibility* report, bool present, bool supported, const char* name) {
  if (!present) return;
  AddUnique(&report->required_features, name);
  if (!supported) AddUnique(&report->unsupported_features, name);
}

}  // namespace

SolverCompatibility CheckSolverCompatibility(const DsaProblem& problem,
                                             const SolverCapabilities& capabilities) {
  SolverCompatibility report;

  bool has_multi_interval = false;
  bool has_flexible_pool = false;
  for (const Buffer& buffer : problem.buffers) {
    has_multi_interval = has_multi_interval || buffer.live_intervals.size() > 1;
    has_flexible_pool = has_flexible_pool || buffer.allowed_pools.size() > 1;
  }
  const bool has_reserved_ranges =
      std::any_of(problem.pools.begin(), problem.pools.end(),
                  [](const Pool& pool) { return !pool.reserved_ranges.empty(); });
  const bool has_bank_geometry =
      std::any_of(problem.pools.begin(), problem.pools.end(),
                  [](const Pool& pool) { return pool.bank_geometry.has_value(); });
  const bool has_reuse_penalties =
      problem.cost_model && !problem.cost_model->reuse_penalties.empty();

  RequireFeature(&report, has_multi_interval, capabilities.multi_interval, "multi_interval");
  RequireFeature(&report, !problem.colocations.empty(), capabilities.colocations, "colocations");
  RequireFeature(&report, !problem.separations.empty(), capabilities.separations, "separations");
  RequireFeature(&report, !problem.temporal_exclusions.empty(), capabilities.temporal_exclusions,
                 "temporal_exclusions");
  RequireFeature(&report, !problem.pinned_allocations.empty(), capabilities.pinned_allocations,
                 "pinned_allocations");
  RequireFeature(&report, has_reserved_ranges, capabilities.reserved_ranges, "reserved_ranges");
  RequireFeature(&report, problem.pools.size() > 1, capabilities.multi_pool, "multi_pool");
  RequireFeature(&report, has_flexible_pool, capabilities.flexible_pool_assignment,
                 "flexible_pool_assignment");
  if (has_reuse_penalties) AddUnique(&report.required_features, "reuse_penalties");
  if (has_bank_geometry) AddUnique(&report.required_features, "bank_geometry");

  if (problem.objective.aggregation == ObjectiveAggregation::kLexicographic &&
      !capabilities.lexicographic_objective) {
    AddUnique(&report.unsupported_objectives, "aggregation:lexicographic");
  }
  for (ObjectiveMetric metric : problem.objective.terms) {
    bool supported = true;
    switch (metric) {
      case ObjectiveMetric::kCapacityOverflow:
        supported = capabilities.capacity_objective;
        break;
      case ObjectiveMetric::kTotalPeak:
      case ObjectiveMetric::kMaxPeak:
        supported = capabilities.peak_objective;
        break;
      case ObjectiveMetric::kReuseCost:
        supported = capabilities.reuse_cost;
        break;
      case ObjectiveMetric::kBankCost:
        supported = capabilities.bank_cost;
        break;
    }
    if (!supported) {
      AddUnique(&report.unsupported_objectives, std::string("metric:") + ToString(metric));
    }
  }
  return report;
}

}  // namespace dsa

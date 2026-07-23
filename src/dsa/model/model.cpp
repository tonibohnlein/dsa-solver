#include "dsa/model/model.h"

namespace dsa {

const Buffer* DsaProblem::FindBuffer(BufferId id) const noexcept {
  for (const Buffer& buffer : buffers) {
    if (buffer.id == id) return &buffer;
  }
  return nullptr;
}

const Pool* DsaProblem::FindPool(PoolId id) const noexcept {
  for (const Pool& pool : pools) {
    if (pool.id == id) return &pool;
  }
  return nullptr;
}

const Placement* DsaSolution::Find(BufferId id) const noexcept {
  const auto it = placements.find(id);
  return it == placements.end() ? nullptr : &it->second;
}

const char* ToString(SolveStatus status) noexcept {
  switch (status) {
    case SolveStatus::kFeasible:
      return "feasible";
    case SolveStatus::kInfeasibleProven:
      return "infeasible_proven";
    case SolveStatus::kBestEffortNoFit:
      return "best_effort_no_fit";
    case SolveStatus::kTimeout:
      return "timeout";
    case SolveStatus::kUnsupported:
      return "unsupported";
    case SolveStatus::kInvalidProblem:
      return "invalid_problem";
  }
  return "unknown";
}

const char* ToString(SeparationReason reason) noexcept {
  switch (reason) {
    case SeparationReason::kGeneric:
      return "generic";
    case SeparationReason::kPipelineStage:
      return "pipeline_stage";
    case SeparationReason::kTargetHazard:
      return "target_hazard";
    case SeparationReason::kSemanticNoAlias:
      return "semantic_no_alias";
  }
  return "unknown";
}

const char* ToString(ObjectiveMetric metric) noexcept {
  switch (metric) {
    case ObjectiveMetric::kCapacityOverflow:
      return "capacity_overflow";
    case ObjectiveMetric::kTotalPeak:
      return "total_peak";
    case ObjectiveMetric::kMaxPeak:
      return "max_peak";
    case ObjectiveMetric::kReuseCost:
      return "reuse_cost";
    case ObjectiveMetric::kBankCost:
      return "bank_cost";
  }
  return "unknown";
}

const char* ToString(ObjectiveAggregation aggregation) noexcept {
  switch (aggregation) {
    case ObjectiveAggregation::kLexicographic:
      return "lexicographic";
  }
  return "unknown";
}

ObjectiveSpec MinimizePeakObjective() {
  return {ObjectiveAggregation::kLexicographic,
          {ObjectiveMetric::kTotalPeak, ObjectiveMetric::kMaxPeak}};
}

ObjectiveSpec FitThenMinimizeReuseCostObjective() {
  return {ObjectiveAggregation::kLexicographic,
          {ObjectiveMetric::kCapacityOverflow, ObjectiveMetric::kReuseCost}};
}

}  // namespace dsa

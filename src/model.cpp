#include "dsa/model.h"

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

}  // namespace dsa

#include "dsa/xla_heap_solver.h"

#include <string>
#include <vector>

#include "detail/placement_engine.h"
#include "dsa/validator.h"

namespace dsa {

const char* XlaHeapSolver::Name() const noexcept { return "xla_heap"; }

SolverCapabilities XlaHeapSolver::Capabilities() const noexcept {
  SolverCapabilities capabilities;
  capabilities.colocations = true;
  capabilities.lexicographic_objective = true;
  capabilities.capacity_objective = true;
  capabilities.peak_objective = true;
  return capabilities;
}

DsaResult XlaHeapSolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    DsaResult result;
    const std::vector<std::string> validation = ValidateProblem(problem);
    if (!validation.empty()) {
      result.status = SolveStatus::kInvalidProblem;
      result.diagnostics = validation;
      return result;
    }
    result.status = SolveStatus::kUnsupported;
    for (const std::string& feature : compatibility.unsupported_features) {
      result.diagnostics.push_back("xla_heap does not support feature '" + feature + "'");
    }
    for (const std::string& objective : compatibility.unsupported_objectives) {
      result.diagnostics.push_back("xla_heap does not support objective '" + objective + "'");
    }
    return result;
  }

  DsaResult result =
      detail::PlaceWithOrder(problem, {}, detail::PlacementStrategy::kXlaSpatialBestFit);
  result.diagnostics.push_back("xla_heap uses OpenXLA spatial decreasing-size/best-fit policy");
  return result;
}

}  // namespace dsa

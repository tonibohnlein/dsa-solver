#include "dsa/first_fit_solver.h"

#include <string>
#include <vector>

#include "detail/placement_engine.h"
#include "dsa/validator.h"

namespace dsa {

const char* FirstFitSolver::Name() const noexcept { return "first_fit"; }

SolverCapabilities FirstFitSolver::Capabilities() const noexcept {
  SolverCapabilities capabilities;
  capabilities.multi_interval = true;
  capabilities.colocations = true;
  capabilities.separations = true;
  capabilities.temporal_exclusions = true;
  capabilities.pinned_allocations = true;
  capabilities.reserved_ranges = true;
  capabilities.multi_pool = true;
  capabilities.whole_slot_reuse = true;
  capabilities.lexicographic_objective = true;
  capabilities.capacity_objective = true;
  capabilities.peak_objective = true;
  return capabilities;
}

DsaResult FirstFitSolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.StructurallyCompatible()) {
    DsaResult result;
    const std::vector<std::string> validation = ValidateProblem(problem);
    if (!validation.empty()) {
      result.status = SolveStatus::kInvalidProblem;
      result.diagnostics = validation;
      return result;
    }
    result.status = SolveStatus::kUnsupported;
    for (const std::string& feature : compatibility.unsupported_features) {
      result.diagnostics.push_back("first_fit does not support feature '" + feature + "'");
    }
    return result;
  }
  DsaResult result = detail::PlaceWithOrder(problem, {});
  for (const std::string& objective : compatibility.unsupported_objectives) {
    result.diagnostics.push_back(
        "first_fit provides a structural baseline but does not optimize '" + objective + "'");
  }
  return result;
}

}  // namespace dsa

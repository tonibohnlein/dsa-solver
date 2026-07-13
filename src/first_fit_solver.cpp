#include "dsa/first_fit_solver.h"

#include "detail/placement_engine.h"

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
  return capabilities;
}

DsaResult FirstFitSolver::Solve(const DsaProblem& problem) const {
  return detail::PlaceWithOrder(problem, detail::DefaultPlacementOrder(problem));
}

}  // namespace dsa

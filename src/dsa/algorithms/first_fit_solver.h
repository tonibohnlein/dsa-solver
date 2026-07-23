#ifndef DSA_FIRST_FIT_SOLVER_H_
#define DSA_FIRST_FIT_SOLVER_H_

#include "dsa/algorithms/solver.h"

namespace dsa {

// Deterministic first-fit control. Standard DSA uses decreasing size. DSA-RP
// additionally evaluates birth-time and soft-incident-weight orders without
// consulting penalties during placement, then rescoring the three candidates.
class FirstFitSolver final : public DsaSolver {
 public:
  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;
};

}  // namespace dsa

#endif  // DSA_FIRST_FIT_SOLVER_H_

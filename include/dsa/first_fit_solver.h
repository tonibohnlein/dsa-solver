#ifndef DSA_FIRST_FIT_SOLVER_H_
#define DSA_FIRST_FIT_SOLVER_H_

#include "dsa/solver.h"

namespace dsa {

// Deterministic first-fit-by-lifetime placement. Colocation classes are placed
// as super-buffers, ordered by decreasing size, then by lifetime and id.
class FirstFitSolver final : public DsaSolver {
 public:
  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;
};

}  // namespace dsa

#endif  // DSA_FIRST_FIT_SOLVER_H_

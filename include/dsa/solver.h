#ifndef DSA_SOLVER_H_
#define DSA_SOLVER_H_

#include "dsa/model.h"

namespace dsa {

struct SolverCapabilities {
  bool multi_interval = false;
  bool reuse_cost = false;
  bool bank_cost = false;
  bool colocations = false;
  bool separations = false;
  bool temporal_exclusions = false;
  bool pinned_allocations = false;
  bool reserved_ranges = false;
  bool multi_pool = false;
  bool flexible_pool_assignment = false;
};

class DsaSolver {
 public:
  virtual ~DsaSolver() = default;

  [[nodiscard]] virtual const char* Name() const noexcept = 0;
  [[nodiscard]] virtual SolverCapabilities Capabilities() const noexcept = 0;
  [[nodiscard]] virtual DsaResult Solve(const DsaProblem& problem) const = 0;
};

}  // namespace dsa

#endif  // DSA_SOLVER_H_

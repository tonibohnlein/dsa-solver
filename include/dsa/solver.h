#ifndef DSA_SOLVER_H_
#define DSA_SOLVER_H_

#include <string>
#include <vector>

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

  bool lexicographic_objective = false;
  bool capacity_objective = false;
  bool peak_objective = false;
};

// Compatibility is split into structural and objective support so benchmark
// reports can distinguish "cannot produce a valid solution" from "can provide
// a baseline, but does not optimize every requested metric".
struct SolverCompatibility {
  std::vector<std::string> required_features;
  std::vector<std::string> unsupported_features;
  std::vector<std::string> unsupported_objectives;

  [[nodiscard]] bool StructurallyCompatible() const noexcept {
    return unsupported_features.empty();
  }
  [[nodiscard]] bool ObjectiveCompatible() const noexcept { return unsupported_objectives.empty(); }
  [[nodiscard]] bool Compatible() const noexcept {
    return StructurallyCompatible() && ObjectiveCompatible();
  }
};

[[nodiscard]] SolverCompatibility CheckSolverCompatibility(const DsaProblem& problem,
                                                           const SolverCapabilities& capabilities);

class DsaSolver {
 public:
  virtual ~DsaSolver() = default;

  [[nodiscard]] virtual const char* Name() const noexcept = 0;
  [[nodiscard]] virtual SolverCapabilities Capabilities() const noexcept = 0;
  [[nodiscard]] virtual DsaResult Solve(const DsaProblem& problem) const = 0;
};

}  // namespace dsa

#endif  // DSA_SOLVER_H_

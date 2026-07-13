#ifndef DSA_LOCAL_SEARCH_SOLVER_H_
#define DSA_LOCAL_SEARCH_SOLVER_H_

#include <cstddef>
#include <cstdint>

#include "dsa/solver.h"

namespace dsa {

enum class LocalSearchObjective {
  // Portable benchmark mode: minimize peak. Reuse cost is only a tie-break.
  kMinimizePeak,

  // Compiler-overlay mode: fit every pool first, then minimize reuse cost;
  // below-cap peak is a final tie-break.
  kFitThenMinimizeReuseCost,
};

struct LocalSearchOptions {
  std::uint64_t seed = 0;
  std::size_t max_iterations = 20'000;
  std::size_t restarts = 8;
  std::size_t stagnation_limit = 500;
  LocalSearchObjective objective = LocalSearchObjective::kMinimizePeak;
};

// Iterated local search over placement orderings. It starts from the
// deterministic first-fit order, explores swap/insert/reverse neighborhoods,
// and uses seeded perturbation/restarts to escape local minima. This is a solid
// ordering-search baseline; placement-level neighborhoods can be added behind
// the same DsaSolver interface.
class LocalSearchSolver final : public DsaSolver {
 public:
  explicit LocalSearchSolver(LocalSearchOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  LocalSearchOptions options_;
};

}  // namespace dsa

#endif  // DSA_LOCAL_SEARCH_SOLVER_H_

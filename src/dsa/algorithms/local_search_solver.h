#ifndef DSA_LOCAL_SEARCH_SOLVER_H_
#define DSA_LOCAL_SEARCH_SOLVER_H_

#include <cstddef>
#include <cstdint>

#include "dsa/algorithms/solver.h"

namespace dsa {

struct LocalSearchOptions {
  std::uint64_t seed = 0;
  // Global placement-order decoder evaluation budget, including deterministic
  // first-fit initialization. Zero disables search and returns that baseline.
  std::size_t max_iterations = 20'000;
  std::size_t restarts = 8;
  std::size_t stagnation_limit = 500;
};

// Iterated local search over placement orderings. It starts from the
// deterministic first-fit order, explores swap/insert/reverse neighborhoods,
// and uses seeded perturbation/restarts to escape local minima. Restart starts
// and stagnation repairs consume the same global candidate-evaluation budget.
// This is a solid
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

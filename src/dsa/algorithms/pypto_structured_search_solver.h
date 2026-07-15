#ifndef DSA_PYPTO_STRUCTURED_SEARCH_SOLVER_H_
#define DSA_PYPTO_STRUCTURED_SEARCH_SOLVER_H_

#include <cstddef>
#include <cstdint>

#include "dsa/algorithms/solver.h"

namespace dsa {

struct PyptoStructuredSearchOptions {
  std::uint64_t seed = 0;
  std::size_t max_iterations = 20'000;
  std::size_t restarts = 8;
  std::size_t stagnation_limit = 500;
};

// PyPTO-specific ordering search. It retains the generic swap/insertion/range
// neighborhoods and adds pipeline-group block moves, semantic-alias priority
// moves, and reuse-penalty endpoint moves. Feasibility and the lexicographic
// objective remain entirely defined by DsaProblem.
class PyptoStructuredSearchSolver final : public DsaSolver {
 public:
  explicit PyptoStructuredSearchSolver(PyptoStructuredSearchOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  PyptoStructuredSearchOptions options_;
};

}  // namespace dsa

#endif  // DSA_PYPTO_STRUCTURED_SEARCH_SOLVER_H_

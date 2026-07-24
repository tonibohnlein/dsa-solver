// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef DSA_REUSE_PENALTY_LOCAL_SEARCH_SOLVER_H_
#define DSA_REUSE_PENALTY_LOCAL_SEARCH_SOLVER_H_

#include <cstddef>
#include <cstdint>

#include "dsa/algorithms/solver.h"

namespace dsa {

struct ReusePenaltyLocalSearchOptions {
  std::uint64_t seed = 0;
  std::size_t max_evaluations = 20'000;
  std::size_t restarts = 8;
  std::size_t stagnation_limit = 250;
  std::size_t soft_moves_per_iteration = 64;
  std::size_t order_moves_per_iteration = 16;
  // Initial probability of accepting a non-improving proposal. The
  // probability decays linearly to zero over max_evaluations.
  double initial_worse_acceptance_probability = 0.05;
};

// Local search over both a first-fit decode order and a set of soft edges
// temporarily promoted to hard separations.
class ReusePenaltyLocalSearchSolver final : public DsaSolver {
 public:
  explicit ReusePenaltyLocalSearchSolver(ReusePenaltyLocalSearchOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  ReusePenaltyLocalSearchOptions options_;
};

}  // namespace dsa

#endif  // DSA_REUSE_PENALTY_LOCAL_SEARCH_SOLVER_H_

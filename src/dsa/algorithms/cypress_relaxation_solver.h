// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef DSA_CYPRESS_RELAXATION_SOLVER_H_
#define DSA_CYPRESS_RELAXATION_SOLVER_H_

#include "dsa/algorithms/solver.h"

namespace dsa {

// Frozen baseline for the allocation policy described by Yadav et al. in
// "Task-Based Tensor Computations on Modern GPUs" (PLDI 2025). The paper does
// not specify auxiliary-edge deletion order; this implementation uses stable
// buffer-ID order and reports that deliberate choice in diagnostics.
class CypressRelaxationSolver final : public DsaSolver {
 public:
  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;
};

}  // namespace dsa

#endif  // DSA_CYPRESS_RELAXATION_SOLVER_H_

// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef DSA_REUSE_PENALTY_BASELINE_SOLVERS_H_
#define DSA_REUSE_PENALTY_BASELINE_SOLVERS_H_

#include "dsa/algorithms/solver.h"

namespace dsa {

// Canonical greedy (CG) baseline from the DSA-RP algorithms section. For each
// buffer it evaluates offset zero and the aligned tops of already placed hard
// or soft neighbors, choosing minimum incremental reuse cost and then the
// lowest offset.
class CanonicalGreedySolver final : public DsaSolver {
 public:
  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;
};

// Promote-and-repair (PR) baseline from the DSA-RP algorithms section. It
// initially treats every reuse-penalty pair as a separation. If the decoded
// placement exceeds capacity, it follows a support chain from an overflowing
// buffer and demotes the cheapest active soft edge on that chain.
class PromoteRepairSolver final : public DsaSolver {
 public:
  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;
};

}  // namespace dsa

#endif  // DSA_REUSE_PENALTY_BASELINE_SOLVERS_H_

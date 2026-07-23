// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef DSA_REUSE_PENALTY_SCALE_SOLVER_H_
#define DSA_REUSE_PENALTY_SCALE_SOLVER_H_

#include <cstddef>
#include <cstdint>

#include "dsa/algorithms/solver.h"

namespace dsa {

struct ScaleSeparatedGridDpOptions {
  // Require the smallest endpoint of a soft edge to be at least C / this
  // denominator. Smaller values mean stronger scale separation.
  std::uint64_t max_large_fraction_denominator = 8;

  // epsilon = 1 / epsilon_denominator for the augmented large-buffer band.
  std::uint64_t epsilon_denominator = 2;

  // Maximum number of large-buffer births spanned by one soft edge.
  std::size_t max_soft_span = 4;

  // Resource guards. Zero disables the corresponding guard.
  std::uint64_t max_dp_states = 2'000'000;
  std::uint64_t max_dp_transitions = 20'000'000;
  std::size_t max_grid_offsets = 256;
};

// Bicriteria solver for scale-separated, bounded-span DSA-RP. Large buffers
// are optimized by a rounded-offset sweep DP; soft-edge-free small buffers are
// placed in stacked harmonic tracks. The result can exceed the input capacity
// and is then reported as BestEffortNoFit with a structurally valid solution.
class ScaleSeparatedGridDpSolver final : public DsaSolver {
 public:
  explicit ScaleSeparatedGridDpSolver(ScaleSeparatedGridDpOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  ScaleSeparatedGridDpOptions options_;
};

}  // namespace dsa

#endif  // DSA_REUSE_PENALTY_SCALE_SOLVER_H_

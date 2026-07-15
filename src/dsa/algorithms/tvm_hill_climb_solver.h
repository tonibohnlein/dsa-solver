// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0
//
// Behavioral reimplementation of Apache TVM USMP's hill-climb policy. See
// NOTICE and docs/tvm_hill_climb.md for provenance and deliberate differences.

#ifndef DSA_TVM_HILL_CLIMB_SOLVER_H_
#define DSA_TVM_HILL_CLIMB_SOLVER_H_

#include <cstddef>
#include <cstdint>
#include <optional>

#include "dsa/algorithms/solver.h"

namespace dsa {

struct TvmHillClimbOptions {
  std::uint64_t seed = 0;
  std::size_t max_attempts = 500;

  // TVM uses 50 in a rapidly decaying, percentage-based acceptance rule.
  // Set to zero for strict hill climbing.
  std::uint32_t worse_move_scale_percent = 50;

  // Corresponds to TVM USMP's memory-pressure stopping target. If absent, the
  // entire attempt budget is used.
  std::optional<std::uint64_t> target_total_peak;
};

// A behavioral reimplementation of Apache TVM USMP's hill-climb search policy:
// start from decreasing size/conflict degree, repeatedly decode an ordering
// with first-fit, then swap earlier first/second-level conflict neighbors of a
// buffer touching a pool's high-water mark.
//
// The search policy is kept distinct from LocalSearchSolver so it is a named
// literature baseline. Its decoder is this library's generalized placement
// engine, which safely extends the policy to PyPTO constraints and objectives.
class TvmHillClimbSolver final : public DsaSolver {
 public:
  explicit TvmHillClimbSolver(TvmHillClimbOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  TvmHillClimbOptions options_;
};

}  // namespace dsa

#endif  // DSA_TVM_HILL_CLIMB_SOLVER_H_

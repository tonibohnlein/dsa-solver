// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef DSA_REUSE_PENALTY_TREEWIDTH_SOLVER_H_
#define DSA_REUSE_PENALTY_TREEWIDTH_SOLVER_H_

#include <cstddef>
#include <cstdint>

#include "dsa/algorithms/solver.h"

namespace dsa {

struct TreewidthPartitionDpOptions {
  // The induced width of the deterministic min-fill elimination order.
  std::size_t max_treewidth = 14;

  // Maximum restricted-growth partition states evaluated for one eliminated
  // variable. Zero disables the guard.
  std::uint64_t max_table_entries = 4'000'000;
};

// Exact minimum-reuse-cost solver for uniform-unit instances whose combined
// hard-and-soft graph has a small min-fill elimination width.
class TreewidthPartitionDpSolver final : public DsaSolver {
 public:
  explicit TreewidthPartitionDpSolver(TreewidthPartitionDpOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  TreewidthPartitionDpOptions options_;
};

}  // namespace dsa

#endif  // DSA_REUSE_PENALTY_TREEWIDTH_SOLVER_H_

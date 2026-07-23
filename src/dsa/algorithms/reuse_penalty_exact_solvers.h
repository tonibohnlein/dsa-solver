// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef DSA_REUSE_PENALTY_EXACT_SOLVERS_H_
#define DSA_REUSE_PENALTY_EXACT_SOLVERS_H_

#include <cstddef>
#include <cstdint>

#include "dsa/algorithms/solver.h"

namespace dsa {

struct CanonicalBranchAndBoundOptions {
  // Zero means unlimited. A completed search proves the reported optimum or
  // infeasibility; exhausting this budget returns kTimeout.
  std::uint64_t max_search_nodes = 2'000'000;
};

class CanonicalBranchAndBoundSolver final : public DsaSolver {
 public:
  explicit CanonicalBranchAndBoundSolver(CanonicalBranchAndBoundOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  CanonicalBranchAndBoundOptions options_;
};

struct ImplicitHittingSetOptions {
  // Per canonical feasibility-oracle call. Zero means unlimited.
  std::uint64_t max_oracle_nodes = 2'000'000;
  std::size_t max_iterations = 1'000;
  bool shrink_cores = true;
};

class ImplicitHittingSetSolver final : public DsaSolver {
 public:
  explicit ImplicitHittingSetSolver(ImplicitHittingSetOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  ImplicitHittingSetOptions options_;
};

}  // namespace dsa

#endif  // DSA_REUSE_PENALTY_EXACT_SOLVERS_H_

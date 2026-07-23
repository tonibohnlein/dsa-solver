// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef DSA_REUSE_PENALTY_PORTFOLIO_SOLVERS_H_
#define DSA_REUSE_PENALTY_PORTFOLIO_SOLVERS_H_

#include <cstddef>
#include <cstdint>

#include "dsa/algorithms/reuse_penalty_exact_solvers.h"
#include "dsa/algorithms/reuse_penalty_treewidth_solver.h"
#include "dsa/algorithms/solver.h"

namespace dsa {

struct CapacityTwoExactOptions {
  // Maximum component-flip search nodes per independent pool. Zero is
  // unlimited.
  std::uint64_t max_search_nodes = 2'000'000;
};

// Exact specialization for unit-size arenas holding at most two units. Hard
// components are bipartitioned; one binary flip per component is optimized
// against weighted soft same-color penalties.
class CapacityTwoExactSolver final : public DsaSolver {
 public:
  explicit CapacityTwoExactSolver(CapacityTwoExactOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  CapacityTwoExactOptions options_;
};

struct SpanOneMinCostFlowOptions {
  // Guards accidental construction of very large phase matchings.
  std::size_t max_phase_buffers = 512;
};

// Exact minimum-reuse-cost specialization for unit-size phase instances whose
// hard components are cliques and whose soft edges connect adjacent phases.
class SpanOneMinCostFlowSolver final : public DsaSolver {
 public:
  explicit SpanOneMinCostFlowSolver(SpanOneMinCostFlowOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  SpanOneMinCostFlowOptions options_;
};

struct ReusePenaltyPortfolioOptions {
  SpanOneMinCostFlowOptions span_one;
  CapacityTwoExactOptions capacity_two;
  TreewidthPartitionDpOptions treewidth;
  CanonicalBranchAndBoundOptions general;
};

// Dispatches to the first applicable exact specialization, then to canonical
// branch-and-bound. The selected method is recorded in solver_metrics.
class ReusePenaltyPortfolioSolver final : public DsaSolver {
 public:
  explicit ReusePenaltyPortfolioSolver(ReusePenaltyPortfolioOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  ReusePenaltyPortfolioOptions options_;
};

}  // namespace dsa

#endif  // DSA_REUSE_PENALTY_PORTFOLIO_SOLVERS_H_

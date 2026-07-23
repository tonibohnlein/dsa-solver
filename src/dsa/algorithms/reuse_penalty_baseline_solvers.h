// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef DSA_REUSE_PENALTY_BASELINE_SOLVERS_H_
#define DSA_REUSE_PENALTY_BASELINE_SOLVERS_H_

#include <cstddef>
#include <cstdint>

#include "dsa/algorithms/solver.h"

namespace dsa {

struct CanonicalGreedyOptions {
  std::uint64_t seed = 0;
  std::size_t random_restarts = 4;
};

// Canonical greedy (CG) baseline from the DSA-RP algorithms section. For each
// buffer it evaluates offset zero and the aligned tops of already placed hard
// or soft neighbors, choosing minimum incremental reuse cost and then the
// lowest offset.
class CanonicalGreedySolver final : public DsaSolver {
 public:
  explicit CanonicalGreedySolver(CanonicalGreedyOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  CanonicalGreedyOptions options_;
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

struct PromoteAllOptions {
  std::size_t max_search_nodes = 2'000'000;
};

// Exact zero-penalty feasibility oracle. It promotes every soft edge and
// exhaustively searches the canonical support space, returning feasible,
// proven infeasible, or timeout at the configured node budget.
class PromoteAllSolver final : public DsaSolver {
 public:
  explicit PromoteAllSolver(PromoteAllOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  PromoteAllOptions options_;
};

struct UnitRandomColoringOptions {
  std::uint64_t seed = 0;
  std::size_t samples = 1;
};

// Uniform-color control for the geometry-free, unit-size specialization.
// Each node independently selects one of the capacity colors.
class UnitRandomColoringSolver final : public DsaSolver {
 public:
  explicit UnitRandomColoringSolver(UnitRandomColoringOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  UnitRandomColoringOptions options_;
};

struct UnitLowRankRoundingOptions {
  std::uint64_t seed = 0;
  std::size_t relaxation_iterations = 200;
  std::size_t rounding_samples = 64;
  std::size_t rank = 16;
  std::size_t max_nodes = 512;
};

// Numerical low-rank Gaussian heuristic for the geometry-free unit-size
// specialization. It uses a nonconvex vector relaxation inspired by Max-k-cut
// rounding; it is not an SDP solver and carries no Frieze-Jerrum guarantee.
class UnitLowRankRoundingSolver final : public DsaSolver {
 public:
  explicit UnitLowRankRoundingSolver(UnitLowRankRoundingOptions options = {});

  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;

 private:
  UnitLowRankRoundingOptions options_;
};

}  // namespace dsa

#endif  // DSA_REUSE_PENALTY_BASELINE_SOLVERS_H_

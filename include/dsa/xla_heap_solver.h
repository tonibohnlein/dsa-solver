// SPDX-License-Identifier: Apache-2.0
// Independently written behavioral reimplementation of OpenXLA's
// GlobalDecreasingSizeBestFitHeap spatial policy. No OpenXLA source is
// vendored or linked; see NOTICE and docs/xla_heap.md.

#ifndef DSA_XLA_HEAP_SOLVER_H_
#define DSA_XLA_HEAP_SOLVER_H_

#include "dsa/solver.h"

namespace dsa {

// Frozen behavioral baseline for OpenXLA's spatial
// GlobalDecreasingSizeBestFitHeap policy. It is intentionally a core DSA
// baseline: single pool, one interval per buffer, optional colocations, and a
// peak objective. PyPTO extensions are compared through sound core
// relaxations, not silently discarded.
class XlaHeapSolver final : public DsaSolver {
 public:
  [[nodiscard]] const char* Name() const noexcept override;
  [[nodiscard]] SolverCapabilities Capabilities() const noexcept override;
  [[nodiscard]] DsaResult Solve(const DsaProblem& problem) const override;
};

}  // namespace dsa

#endif  // DSA_XLA_HEAP_SOLVER_H_

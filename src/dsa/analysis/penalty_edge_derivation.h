// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef DSA_ANALYSIS_PENALTY_EDGE_DERIVATION_H_
#define DSA_ANALYSIS_PENALTY_EDGE_DERIVATION_H_

#include <cstdint>
#include <utility>
#include <vector>

#include "dsa/model/model.h"

namespace dsa {

using OperationId = std::uint32_t;
using StreamId = std::uint32_t;

enum class AccessKind : std::uint8_t {
  kRead,
  kWrite,
};

struct ScheduledOperation {
  OperationId id = 0;
  StreamId stream = 0;
  std::uint64_t issue_index = 0;
  std::int64_t schedule_time = 0;
};

struct ScheduledDependency {
  OperationId before = 0;
  OperationId after = 0;
};

struct ScheduledBufferAccess {
  BufferId buffer = 0;
  OperationId operation = 0;
  AccessKind kind = AccessKind::kRead;
};

// A scheduled program has one completion-ordered chain per stream. Cross
// dependencies contain data dependencies and pre-existing synchronization;
// same-stream order is derived from issue_index.
struct ScheduledProgram {
  std::uint32_t stream_count = 0;
  std::vector<ScheduledOperation> operations;
  std::vector<ScheduledDependency> cross_dependencies;
  std::vector<ScheduledBufferAccess> accesses;
};

struct DerivedSyncAnchor {
  OperationId last_access = 0;
  OperationId first_write = 0;

  [[nodiscard]] bool operator==(const DerivedSyncAnchor& other) const noexcept {
    return last_access == other.last_access && first_write == other.first_write;
  }
};

struct DerivedPenaltyEdge {
  BufferId first = 0;
  BufferId second = 0;
  std::uint64_t cost = 0;
  std::vector<DerivedSyncAnchor> needed_syncs;
};

struct PenaltyEdgeDerivation {
  std::vector<DerivedPenaltyEdge> edges;
  std::size_t lifetime_compatible_pairs = 0;
  std::size_t ordered_pairs = 0;
};

// Implements the maximal-access happens-before rule from the DSA-RP model.
// sync_cost[p][q] is the rung-1 cost of ordering an access on stream p
// before a first write on stream q. Zero-cost hazards are omitted.
[[nodiscard]] PenaltyEdgeDerivation DerivePenaltyEdges(
    const DsaProblem& problem, const ScheduledProgram& program,
    const std::vector<std::vector<std::uint64_t>>& sync_cost);

// Adds derived edges as generic reuse penalties and selects the
// capacity-constrained DSA-RP objective.
void ApplyPenaltyEdgeDerivation(DsaProblem* problem, const PenaltyEdgeDerivation& derivation);

}  // namespace dsa

#endif  // DSA_ANALYSIS_PENALTY_EDGE_DERIVATION_H_

// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef DSA_DETAIL_CANONICAL_EXACT_SEARCH_H_
#define DSA_DETAIL_CANONICAL_EXACT_SEARCH_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "dsa/algorithms/reuse_penalty_search.h"
#include "dsa/model/model.h"

namespace dsa::detail {

struct CanonicalExactSearchOptions {
  std::uint64_t node_limit = 2'000'000;
  bool stop_after_first = false;
  // Optional validated node offsets used only as an upper bound. The search
  // still exhausts its canonical state space unless stop_after_first is set.
  std::optional<std::vector<std::uint64_t>> incumbent_offsets;
};

struct CanonicalExactSearchResult {
  SolveStatus status = SolveStatus::kUnsupported;
  std::optional<std::vector<std::uint64_t>> offsets;
  std::uint64_t search_nodes = 0;
  std::uint64_t candidate_branches = 0;
  std::uint64_t bound_prunes = 0;
};

// Exhaustively enumerates canonical placements. Within each independent pool,
// nodes are appended in nondecreasing offset order; each offset is zero, a
// reserved-range end, or the aligned top of an already placed hard/soft
// neighbor. A complete search proves optimality or infeasibility under the
// canonical-support theorem used by the DSA-RP paper.
[[nodiscard]] CanonicalExactSearchResult RunCanonicalExactSearch(
    const DsaProblem& problem, const ReusePenaltySearchSpace& search,
    const CanonicalExactSearchOptions& options);

}  // namespace dsa::detail

#endif  // DSA_DETAIL_CANONICAL_EXACT_SEARCH_H_

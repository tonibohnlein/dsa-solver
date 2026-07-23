// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef DSA_DETAIL_REUSE_PENALTY_SEARCH_H_
#define DSA_DETAIL_REUSE_PENALTY_SEARCH_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dsa/algorithms/placement_engine.h"
#include "dsa/model/model.h"

namespace dsa::detail {

using ReuseNodePair = std::pair<std::size_t, std::size_t>;

// Normalized search view shared by every DSA-RP algorithm. Nodes are
// colocation classes, hard_neighbors contains temporal and explicit
// keep-apart constraints, and duplicate soft records between two classes are
// aggregated into one weight.
struct ReusePenaltySearchSpace {
  PlacementSearchSpace placement;
  std::unordered_map<BufferId, std::size_t> node_by_buffer;
  std::vector<std::set<std::size_t>> hard_neighbors;
  std::map<ReuseNodePair, std::uint64_t> soft_weights;
  std::vector<std::vector<std::pair<std::size_t, std::uint64_t>>> soft_neighbors;
};

[[nodiscard]] bool AddOverflows(std::uint64_t first, std::uint64_t second) noexcept;
[[nodiscard]] std::uint64_t SaturatingAdd(std::uint64_t first, std::uint64_t second) noexcept;
[[nodiscard]] std::optional<std::uint64_t> AlignUp(std::uint64_t value,
                                                   std::uint64_t alignment) noexcept;
[[nodiscard]] ReuseNodePair CanonicalNodePair(std::size_t first, std::size_t second) noexcept;

[[nodiscard]] ReusePenaltySearchSpace BuildReusePenaltySearchSpace(const DsaProblem& problem);
[[nodiscard]] std::vector<std::size_t> DefaultReuseNodeOrder(const DsaProblem& problem,
                                                             const ReusePenaltySearchSpace& search);

[[nodiscard]] bool FitsReuseNodeAt(const DsaProblem& problem, const ReusePenaltySearchSpace& search,
                                   const std::vector<bool>& placed,
                                   const std::vector<std::uint64_t>& offsets, std::size_t current,
                                   std::uint64_t offset);
[[nodiscard]] std::uint64_t IncrementalReuseCost(const ReusePenaltySearchSpace& search,
                                                 const std::vector<bool>& placed,
                                                 const std::vector<std::uint64_t>& offsets,
                                                 std::size_t current, std::uint64_t offset);
[[nodiscard]] std::set<std::uint64_t> CanonicalOffsetsForNode(
    const DsaProblem& problem, const ReusePenaltySearchSpace& search,
    const std::vector<bool>& placed, const std::vector<std::uint64_t>& offsets, std::size_t current,
    bool include_soft_supports);

[[nodiscard]] DsaSolution ExpandReuseNodeSolution(const ReusePenaltySearchSpace& search,
                                                  const std::vector<std::uint64_t>& offsets);
[[nodiscard]] DsaResult BuildValidatedReuseResult(const DsaProblem& problem,
                                                  const ReusePenaltySearchSpace& search,
                                                  const std::vector<std::uint64_t>& offsets,
                                                  SolveStatus status);
[[nodiscard]] DsaProblem WithPromotedReuseEdges(const DsaProblem& problem,
                                                const ReusePenaltySearchSpace& search,
                                                const std::set<ReuseNodePair>& promoted);

}  // namespace dsa::detail

#endif  // DSA_DETAIL_REUSE_PENALTY_SEARCH_H_

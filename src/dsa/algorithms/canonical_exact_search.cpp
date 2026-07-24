// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/canonical_exact_search.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "dsa/algorithms/placement_engine.h"
#include "dsa/algorithms/reuse_penalty_search.h"
#include "dsa/model/model.h"

namespace dsa::detail {
namespace {

using Score = std::vector<std::uint64_t>;

struct PoolSearchResult {
  SolveStatus status = SolveStatus::kUnsupported;
  std::vector<std::uint64_t> offsets;
  std::uint64_t search_nodes = 0;
  std::uint64_t candidate_branches = 0;
  std::uint64_t bound_prunes = 0;
};

Score PoolScore(const DsaProblem& problem, std::uint64_t reuse_cost, std::uint64_t peak) {
  Score score;
  score.reserve(problem.objective.terms.size());
  for (ObjectiveMetric metric : problem.objective.terms) {
    switch (metric) {
      case ObjectiveMetric::kCapacityOverflow:
      case ObjectiveMetric::kBankCost:
        score.push_back(0);
        break;
      case ObjectiveMetric::kReuseCost:
        score.push_back(reuse_cost);
        break;
      case ObjectiveMetric::kTotalPeak:
      case ObjectiveMetric::kMaxPeak:
        score.push_back(peak);
        break;
    }
  }
  return score;
}

std::uint64_t ReservedPeak(const DsaProblem& problem, PoolId pool_id) {
  const Pool* pool = problem.FindPool(pool_id);
  std::uint64_t peak = 0;
  if (pool != nullptr) {
    for (const AddressRange& range : pool->reserved_ranges) {
      peak = std::max(peak, range.end);
    }
  }
  return peak;
}

class PoolCanonicalSearch {
 public:
  PoolCanonicalSearch(const DsaProblem& problem, const ReusePenaltySearchSpace& search, PoolId pool,
                      const CanonicalExactSearchOptions& options,
                      std::uint64_t remaining_node_budget)
      : problem_(problem),
        search_(search),
        pool_(pool),
        options_(options),
        remaining_node_budget_(remaining_node_budget),
        placed_(search.placement.nodes.size(), false),
        offsets_(search.placement.nodes.size(), 0),
        best_offsets_(search.placement.nodes.size(), 0) {
    for (std::size_t index = 0; index < search_.placement.nodes.size(); ++index) {
      if (search_.placement.nodes[index].pool == pool_) {
        nodes_.push_back(index);
      }
    }
    std::sort(nodes_.begin(), nodes_.end(), [&](std::size_t first, std::size_t second) {
      return search_.placement.nodes[first].representative <
             search_.placement.nodes[second].representative;
    });
    best_score_.assign(problem_.objective.terms.size(), std::numeric_limits<std::uint64_t>::max());
  }

  PoolSearchResult Run() {
    Search(0, 0, ReservedPeak(problem_, pool_), std::nullopt);
    PoolSearchResult result;
    result.search_nodes = search_nodes_;
    result.candidate_branches = candidate_branches_;
    result.bound_prunes = bound_prunes_;
    if (limit_hit_) {
      result.status = SolveStatus::kTimeout;
    } else if (!found_) {
      result.status = SolveStatus::kInfeasibleProven;
    } else {
      result.status = SolveStatus::kFeasible;
    }
    if (found_) {
      result.offsets = best_offsets_;
    }
    return result;
  }

 private:
  struct Branch {
    std::uint64_t incremental_cost = 0;
    std::uint64_t offset = 0;
    BufferId representative = 0;
    std::size_t node = 0;
  };

  void Search(std::size_t depth, std::uint64_t current_cost, std::uint64_t current_peak,
              std::optional<std::pair<std::uint64_t, BufferId>> last) {
    if (limit_hit_ || (options_.stop_after_first && found_)) {
      return;
    }
    if (remaining_node_budget_ != 0 && search_nodes_ >= remaining_node_budget_) {
      limit_hit_ = true;
      return;
    }
    ++search_nodes_;

    const Score lower_bound = PoolScore(problem_, current_cost, current_peak);
    if (found_ && lower_bound >= best_score_) {
      ++bound_prunes_;
      return;
    }
    if (depth == nodes_.size()) {
      found_ = true;
      best_score_ = lower_bound;
      best_offsets_ = offsets_;
      return;
    }

    std::vector<Branch> branches;
    for (std::size_t current : nodes_) {
      if (placed_[current]) {
        continue;
      }
      const PlacementSearchNode& node = search_.placement.nodes[current];
      const std::set<std::uint64_t> candidates =
          CanonicalOffsetsForNode(problem_, search_, placed_, offsets_, current, true);
      for (std::uint64_t offset : candidates) {
        if (last.has_value() && (offset < last->first ||
                                 (offset == last->first && node.representative <= last->second))) {
          continue;
        }
        if (!FitsReuseNodeAt(problem_, search_, placed_, offsets_, current, offset)) {
          continue;
        }
        branches.push_back({IncrementalReuseCost(search_, placed_, offsets_, current, offset),
                            offset, node.representative, current});
      }
    }
    std::sort(branches.begin(), branches.end(), [](const Branch& first, const Branch& second) {
      return std::tie(first.incremental_cost, first.offset, first.representative) <
             std::tie(second.incremental_cost, second.offset, second.representative);
    });

    for (const Branch& branch : branches) {
      ++candidate_branches_;
      const PlacementSearchNode& node = search_.placement.nodes[branch.node];
      placed_[branch.node] = true;
      offsets_[branch.node] = branch.offset;
      const std::uint64_t next_cost = SaturatingAdd(current_cost, branch.incremental_cost);
      const std::uint64_t next_peak = std::max(current_peak, branch.offset + node.size);
      Search(depth + 1, next_cost, next_peak, std::make_pair(branch.offset, branch.representative));
      placed_[branch.node] = false;
      if (limit_hit_ || (options_.stop_after_first && found_)) {
        return;
      }
    }
  }

  const DsaProblem& problem_;
  const ReusePenaltySearchSpace& search_;
  PoolId pool_;
  CanonicalExactSearchOptions options_;
  std::uint64_t remaining_node_budget_;
  std::vector<std::size_t> nodes_;
  std::vector<bool> placed_;
  std::vector<std::uint64_t> offsets_;
  std::vector<std::uint64_t> best_offsets_;
  Score best_score_;
  bool found_ = false;
  bool limit_hit_ = false;
  std::uint64_t search_nodes_ = 0;
  std::uint64_t candidate_branches_ = 0;
  std::uint64_t bound_prunes_ = 0;
};

}  // namespace

CanonicalExactSearchResult RunCanonicalExactSearch(const DsaProblem& problem,
                                                   const ReusePenaltySearchSpace& search,
                                                   const CanonicalExactSearchOptions& options) {
  CanonicalExactSearchResult result;
  if (!search.placement.errors.empty()) {
    result.status = SolveStatus::kInvalidProblem;
    return result;
  }
  if (search.placement.flexible_pools) {
    result.status = SolveStatus::kUnsupported;
    return result;
  }

  std::vector<std::uint64_t> offsets(search.placement.nodes.size(), 0);
  std::uint64_t remaining = options.node_limit == 0 ? 0 : options.node_limit;
  for (std::size_t pool_index = 0; pool_index < problem.pools.size(); ++pool_index) {
    const Pool& pool = problem.pools[pool_index];
    const bool has_nodes =
        std::any_of(search.placement.nodes.begin(), search.placement.nodes.end(),
                    [&](const PlacementSearchNode& node) { return node.pool == pool.id; });
    if (!has_nodes) {
      continue;
    }
    if (options.node_limit != 0 && result.search_nodes >= options.node_limit) {
      result.status = SolveStatus::kTimeout;
      return result;
    }
    PoolCanonicalSearch pool_search(problem, search, pool.id, options, remaining);
    const PoolSearchResult pool_result = pool_search.Run();
    result.search_nodes = SaturatingAdd(result.search_nodes, pool_result.search_nodes);
    result.candidate_branches =
        SaturatingAdd(result.candidate_branches, pool_result.candidate_branches);
    result.bound_prunes = SaturatingAdd(result.bound_prunes, pool_result.bound_prunes);
    if (options.node_limit != 0) {
      remaining =
          result.search_nodes >= options.node_limit ? 0 : options.node_limit - result.search_nodes;
    }
    if (pool_result.status != SolveStatus::kFeasible) {
      if (pool_result.status == SolveStatus::kTimeout && !pool_result.offsets.empty()) {
        bool has_later_nodes = false;
        for (std::size_t later = pool_index + 1; later < problem.pools.size(); ++later) {
          has_later_nodes =
              has_later_nodes ||
              std::any_of(search.placement.nodes.begin(), search.placement.nodes.end(),
                          [&](const PlacementSearchNode& node) {
                            return node.pool == problem.pools[later].id;
                          });
        }
        if (!has_later_nodes) {
          for (std::size_t index = 0; index < offsets.size(); ++index) {
            if (search.placement.nodes[index].pool == pool.id) {
              offsets[index] = pool_result.offsets[index];
            }
          }
          result.offsets = offsets;
        }
      }
      result.status = pool_result.status;
      return result;
    }
    for (std::size_t index = 0; index < offsets.size(); ++index) {
      if (search.placement.nodes[index].pool == pool.id) {
        offsets[index] = pool_result.offsets[index];
      }
    }
  }
  result.status = SolveStatus::kFeasible;
  result.offsets = std::move(offsets);
  return result;
}

}  // namespace dsa::detail

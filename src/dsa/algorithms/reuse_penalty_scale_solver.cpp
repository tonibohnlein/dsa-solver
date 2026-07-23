// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/reuse_penalty_scale_solver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "dsa/algorithms/placement_engine.h"
#include "dsa/algorithms/reuse_penalty_search.h"
#include "dsa/algorithms/solver.h"
#include "dsa/model/model.h"
#include "dsa/model/validator.h"

namespace dsa {
namespace {

constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

struct TimedNode {
  std::size_t node = 0;
  std::int64_t birth = 0;
  std::int64_t death = 0;
  std::uint64_t rounded_size = 0;
};

struct DpEntry {
  std::vector<std::uint32_t> key;
  std::uint64_t cost = 0;
  std::size_t predecessor = kNoIndex;
  std::uint32_t assigned_offset = 0;
};

struct PoolScaleResult {
  SolveStatus status = SolveStatus::kUnsupported;
  std::string diagnostic;
  std::vector<std::uint64_t> offsets;
  std::uint64_t rounded_cost = 0;
  std::uint64_t grid_quantum = 0;
  std::uint64_t large_band_limit = 0;
  std::uint64_t large_peak = 0;
  std::uint64_t small_band_height = 0;
  std::uint64_t dp_states = 0;
  std::uint64_t dp_transitions = 0;
  std::uint64_t small_tracks = 0;
  std::size_t max_window = 0;
  std::size_t max_soft_span = 0;
  std::size_t max_grid_offsets = 0;
  std::size_t large_nodes = 0;
  std::size_t small_nodes = 0;
};

SolverCapabilities ScaleCapabilities() {
  SolverCapabilities capabilities;
  capabilities.multi_interval = true;
  capabilities.reuse_cost = true;
  capabilities.colocations = true;
  capabilities.separations = true;
  capabilities.temporal_exclusions = true;
  capabilities.multi_pool = true;
  capabilities.lexicographic_objective = true;
  capabilities.capacity_objective = true;
  capabilities.peak_objective = true;
  return capabilities;
}

std::optional<std::string> ObjectiveLimitation(const DsaProblem& problem) {
  std::optional<std::size_t> reuse_index;
  std::optional<std::size_t> peak_index;
  for (std::size_t index = 0; index < problem.objective.terms.size(); ++index) {
    const ObjectiveMetric metric = problem.objective.terms[index];
    if (metric == ObjectiveMetric::kReuseCost) {
      reuse_index = index;
    } else if ((metric == ObjectiveMetric::kTotalPeak || metric == ObjectiveMetric::kMaxPeak) &&
               !peak_index.has_value()) {
      peak_index = index;
    }
  }
  if (!reuse_index.has_value()) {
    return "scale_separated_grid_dp requires reuse_cost in the objective";
  }
  if (peak_index.has_value() && peak_index.value() < reuse_index.value()) {
    return "scale_separated_grid_dp requires reuse_cost before peak metrics";
  }
  return std::nullopt;
}

std::optional<std::uint64_t> LeastCommonMultiple(std::uint64_t first, std::uint64_t second) {
  const std::uint64_t divisor = std::gcd(first, second);
  const std::uint64_t reduced = first / divisor;
  if (second != 0 && reduced > std::numeric_limits<std::uint64_t>::max() / second) {
    return std::nullopt;
  }
  return reduced * second;
}

std::uint64_t CeilDivide(std::uint64_t numerator, std::uint64_t denominator) {
  return numerator / denominator + static_cast<std::uint64_t>(numerator % denominator != 0);
}

std::optional<std::pair<std::int64_t, std::int64_t>> LifetimeHull(
    const DsaProblem& problem, const detail::PlacementSearchNode& node) {
  std::int64_t birth = std::numeric_limits<std::int64_t>::max();
  std::int64_t death = std::numeric_limits<std::int64_t>::min();
  for (BufferId member : node.members) {
    const Buffer* buffer = problem.FindBuffer(member);
    if (buffer == nullptr) {
      return std::nullopt;
    }
    for (const Interval& interval : buffer->live_intervals) {
      birth = std::min(birth, interval.lower);
      death = std::max(death, interval.upper);
    }
  }
  if (birth >= death) {
    return std::nullopt;
  }
  return std::make_pair(birth, death);
}

bool RangesOverlap(std::uint64_t first_offset, std::uint64_t first_size,
                   std::uint64_t second_offset, std::uint64_t second_size) {
  return AddressRangesOverlap(first_offset, first_size, second_offset, second_size);
}

std::vector<std::size_t> NodesInPool(const detail::ReusePenaltySearchSpace& search, PoolId pool) {
  std::vector<std::size_t> nodes;
  for (std::size_t node = 0; node < search.placement.nodes.size(); ++node) {
    if (search.placement.nodes[node].pool == pool) {
      nodes.push_back(node);
    }
  }
  return nodes;
}

std::optional<std::uint64_t> MinimumSoftEndpointSize(const detail::ReusePenaltySearchSpace& search,
                                                     PoolId pool) {
  std::optional<std::uint64_t> minimum;
  for (const auto& [pair, weight] : search.soft_weights) {
    static_cast<void>(weight);
    if (search.placement.nodes[pair.first].pool != pool) {
      continue;
    }
    const std::uint64_t pair_minimum =
        std::min(search.placement.nodes[pair.first].size, search.placement.nodes[pair.second].size);
    minimum = minimum.has_value() ? std::min(minimum.value(), pair_minimum) : pair_minimum;
  }
  return minimum;
}

bool IsIntervalSmallBand(const DsaProblem& problem, const detail::ReusePenaltySearchSpace& search,
                         const std::vector<std::size_t>& small_nodes) {
  std::set<std::size_t> small(small_nodes.begin(), small_nodes.end());
  for (std::size_t node : small_nodes) {
    const detail::PlacementSearchNode& value = search.placement.nodes[node];
    if (value.members.size() != 1) {
      return false;
    }
    const Buffer* buffer = problem.FindBuffer(value.members.front());
    if (buffer == nullptr || buffer->live_intervals.size() != 1) {
      return false;
    }
    for (std::size_t neighbor : search.hard_neighbors[node]) {
      if (node >= neighbor || small.count(neighbor) == 0) {
        continue;
      }
      const Buffer* other = problem.FindBuffer(search.placement.nodes[neighbor].members.front());
      if (other == nullptr || !HaveTemporalConflict(problem, *buffer, *other)) {
        return false;
      }
    }
  }
  for (const TemporalExclusion& exclusion : problem.temporal_exclusions) {
    const auto first = search.node_by_buffer.find(exclusion.first);
    const auto second = search.node_by_buffer.find(exclusion.second);
    if (first != search.node_by_buffer.end() && second != search.node_by_buffer.end() &&
        small.count(first->second) != 0 && small.count(second->second) != 0) {
      return false;
    }
  }
  return true;
}

std::size_t SoftSpan(const std::vector<TimedNode>& nodes, std::size_t first, std::size_t second) {
  if (first > second) {
    std::swap(first, second);
  }
  if (nodes[first].death > nodes[second].birth) {
    return 0;
  }
  const auto first_after_death = std::upper_bound(
      nodes.begin(), nodes.end(), nodes[first].death,
      [](std::int64_t value, const TimedNode& node) { return value < node.birth; });
  const auto last_at_birth = std::upper_bound(
      nodes.begin(), nodes.end(), nodes[second].birth,
      [](std::int64_t value, const TimedNode& node) { return value < node.birth; });
  return static_cast<std::size_t>(std::distance(first_after_death, last_at_birth));
}

std::vector<std::vector<std::size_t>> BuildWindows(const std::vector<TimedNode>& nodes,
                                                   const std::vector<std::size_t>& last_partner,
                                                   std::size_t* maximum_window) {
  std::vector<std::vector<std::size_t>> windows(nodes.size());
  for (std::size_t index = 0; index + 1 < nodes.size(); ++index) {
    const std::int64_t next_birth = nodes[index + 1].birth;
    for (std::size_t prior = 0; prior <= index; ++prior) {
      if (nodes[prior].death > next_birth || last_partner[prior] >= index + 1) {
        windows[index].push_back(prior);
      }
    }
    *maximum_window = std::max(*maximum_window, windows[index].size());
  }
  return windows;
}

std::optional<std::uint32_t> OffsetFromState(const std::vector<std::size_t>& window,
                                             const DpEntry& entry, std::size_t rank) {
  const auto found = std::lower_bound(window.begin(), window.end(), rank);
  if (found == window.end() || *found != rank) {
    return std::nullopt;
  }
  return entry.key[static_cast<std::size_t>(std::distance(window.begin(), found))];
}

bool TransitionFits(const detail::ReusePenaltySearchSpace& search,
                    const std::vector<TimedNode>& nodes,
                    const std::vector<std::size_t>& rank_by_node,
                    const std::vector<std::size_t>& previous_window, const DpEntry& previous,
                    std::size_t current_rank, std::uint32_t color, std::uint64_t quantum) {
  const std::size_t current = nodes[current_rank].node;
  const std::uint64_t offset = static_cast<std::uint64_t>(color) * quantum;
  for (std::size_t neighbor : search.hard_neighbors[current]) {
    const std::size_t neighbor_rank = rank_by_node[neighbor];
    if (neighbor_rank == kNoIndex || neighbor_rank >= current_rank) {
      continue;
    }
    const std::optional<std::uint32_t> neighbor_color =
        OffsetFromState(previous_window, previous, neighbor_rank);
    if (!neighbor_color.has_value()) {
      return false;
    }
    const std::uint64_t neighbor_offset =
        static_cast<std::uint64_t>(neighbor_color.value()) * quantum;
    if (RangesOverlap(offset, nodes[current_rank].rounded_size, neighbor_offset,
                      nodes[neighbor_rank].rounded_size)) {
      return false;
    }
  }
  return true;
}

std::optional<std::uint64_t> TransitionCost(const detail::ReusePenaltySearchSpace& search,
                                            const std::vector<TimedNode>& nodes,
                                            const std::vector<std::size_t>& rank_by_node,
                                            const std::vector<std::size_t>& previous_window,
                                            const DpEntry& previous, std::size_t current_rank,
                                            std::uint32_t color, std::uint64_t quantum) {
  const std::size_t current = nodes[current_rank].node;
  const std::uint64_t offset = static_cast<std::uint64_t>(color) * quantum;
  std::uint64_t cost = 0;
  for (const auto& [neighbor, weight] : search.soft_neighbors[current]) {
    const std::size_t neighbor_rank = rank_by_node[neighbor];
    if (neighbor_rank == kNoIndex || neighbor_rank >= current_rank) {
      continue;
    }
    const std::optional<std::uint32_t> neighbor_color =
        OffsetFromState(previous_window, previous, neighbor_rank);
    if (!neighbor_color.has_value()) {
      return std::nullopt;
    }
    const std::uint64_t neighbor_offset =
        static_cast<std::uint64_t>(neighbor_color.value()) * quantum;
    if (RangesOverlap(offset, nodes[current_rank].rounded_size, neighbor_offset,
                      nodes[neighbor_rank].rounded_size)) {
      cost = detail::SaturatingAdd(cost, weight);
    }
  }
  return cost;
}

std::optional<std::vector<std::uint32_t>> RunGridDp(
    const detail::ReusePenaltySearchSpace& search, const std::vector<TimedNode>& nodes,
    const std::vector<std::size_t>& rank_by_node, std::uint64_t quantum, std::uint64_t large_limit,
    const ScaleSeparatedGridDpOptions& options, PoolScaleResult* result) {
  std::vector<std::size_t> last_partner(nodes.size(), 0);
  for (std::size_t rank = 0; rank < nodes.size(); ++rank) {
    last_partner[rank] = rank;
  }
  for (std::size_t rank = 0; rank < nodes.size(); ++rank) {
    const std::size_t node = nodes[rank].node;
    for (std::size_t neighbor : search.hard_neighbors[node]) {
      const std::size_t other = rank_by_node[neighbor];
      if (other != kNoIndex && rank < other) {
        last_partner[rank] = std::max(last_partner[rank], other);
      }
    }
    for (const auto& [neighbor, weight] : search.soft_neighbors[node]) {
      static_cast<void>(weight);
      const std::size_t other = rank_by_node[neighbor];
      if (other != kNoIndex && rank < other) {
        const std::size_t span = SoftSpan(nodes, rank, other);
        result->max_soft_span = std::max(result->max_soft_span, span);
        if (span > options.max_soft_span) {
          result->diagnostic = "scale_separated_grid_dp soft-edge span exceeds max_soft_span";
          return std::nullopt;
        }
        last_partner[rank] = std::max(last_partner[rank], other);
      }
    }
  }

  const std::vector<std::vector<std::size_t>> windows =
      BuildWindows(nodes, last_partner, &result->max_window);
  std::vector<std::vector<DpEntry>> layers;
  layers.reserve(nodes.size());
  const std::vector<DpEntry> initial{{{}, 0, kNoIndex, 0}};
  const std::vector<DpEntry>* previous_entries = &initial;

  for (std::size_t rank = 0; rank < nodes.size(); ++rank) {
    const std::vector<std::size_t> empty_window;
    const std::vector<std::size_t>& previous_window = rank == 0 ? empty_window : windows[rank - 1];
    const std::vector<std::size_t>& next_window = windows[rank];
    if (nodes[rank].rounded_size > large_limit) {
      result->diagnostic = "scale_separated_grid_dp rounded buffer exceeds augmented band";
      return std::nullopt;
    }
    const std::uint64_t color_count = (large_limit - nodes[rank].rounded_size) / quantum + 1;
    if (color_count > options.max_grid_offsets ||
        color_count > std::numeric_limits<std::uint32_t>::max()) {
      result->diagnostic = "scale_separated_grid_dp grid-offset count exceeds guard";
      return std::nullopt;
    }
    result->max_grid_offsets =
        std::max(result->max_grid_offsets, static_cast<std::size_t>(color_count));

    std::vector<DpEntry> next_entries;
    std::map<std::vector<std::uint32_t>, std::size_t> state_index;
    for (std::size_t predecessor = 0; predecessor < previous_entries->size(); ++predecessor) {
      const DpEntry& previous = (*previous_entries)[predecessor];
      for (std::uint64_t color_value = 0; color_value < color_count; ++color_value) {
        if (options.max_dp_transitions != 0 &&
            result->dp_transitions >= options.max_dp_transitions) {
          result->status = SolveStatus::kTimeout;
          result->diagnostic = "scale_separated_grid_dp exhausted max_dp_transitions";
          return std::nullopt;
        }
        ++result->dp_transitions;
        const auto color = static_cast<std::uint32_t>(color_value);
        if (!TransitionFits(search, nodes, rank_by_node, previous_window, previous, rank, color,
                            quantum)) {
          continue;
        }
        const std::optional<std::uint64_t> added_cost = TransitionCost(
            search, nodes, rank_by_node, previous_window, previous, rank, color, quantum);
        if (!added_cost.has_value()) {
          result->status = SolveStatus::kInvalidProblem;
          result->diagnostic = "scale_separated_grid_dp lost a pending soft-edge endpoint";
          return std::nullopt;
        }

        DpEntry candidate;
        candidate.cost = detail::SaturatingAdd(previous.cost, added_cost.value());
        candidate.predecessor = predecessor;
        candidate.assigned_offset = color;
        candidate.key.reserve(next_window.size());
        bool complete = true;
        for (std::size_t kept : next_window) {
          if (kept == rank) {
            candidate.key.push_back(color);
            continue;
          }
          const std::optional<std::uint32_t> kept_color =
              OffsetFromState(previous_window, previous, kept);
          if (!kept_color.has_value()) {
            complete = false;
            break;
          }
          candidate.key.push_back(kept_color.value());
        }
        if (!complete) {
          result->status = SolveStatus::kInvalidProblem;
          result->diagnostic = "scale_separated_grid_dp lost a live endpoint";
          return std::nullopt;
        }

        const auto found = state_index.find(candidate.key);
        if (found == state_index.end()) {
          if (options.max_dp_states != 0 && result->dp_states >= options.max_dp_states) {
            result->status = SolveStatus::kTimeout;
            result->diagnostic = "scale_separated_grid_dp exhausted max_dp_states";
            return std::nullopt;
          }
          state_index.emplace(candidate.key, next_entries.size());
          next_entries.push_back(std::move(candidate));
          ++result->dp_states;
        } else if (candidate.cost < next_entries[found->second].cost) {
          next_entries[found->second] = std::move(candidate);
        }
      }
    }
    if (next_entries.empty()) {
      result->status = SolveStatus::kInfeasibleProven;
      result->diagnostic = "scale_separated_grid_dp found no large-buffer grid placement";
      return std::nullopt;
    }
    layers.push_back(std::move(next_entries));
    previous_entries = &layers.back();
  }

  const auto best = std::min_element(
      layers.back().begin(), layers.back().end(),
      [](const DpEntry& first, const DpEntry& second) { return first.cost < second.cost; });
  std::size_t state = static_cast<std::size_t>(std::distance(layers.back().begin(), best));
  result->rounded_cost = best->cost;
  std::vector<std::uint32_t> colors(nodes.size(), 0);
  for (std::size_t layer = nodes.size(); layer-- > 0;) {
    const DpEntry& entry = layers[layer][state];
    colors[layer] = entry.assigned_offset;
    state = entry.predecessor;
  }
  return colors;
}

std::uint64_t HarmonicUpper(std::uint64_t size, std::uint64_t threshold) {
  std::uint64_t upper = threshold;
  while (upper > 1) {
    const std::uint64_t next = (upper + 1) / 2;
    if (next < size || next == upper) {
      break;
    }
    upper = next;
  }
  return upper;
}

bool PlaceSmallBand(const DsaProblem& problem, const detail::ReusePenaltySearchSpace& search,
                    const std::vector<std::size_t>& small_nodes, std::uint64_t threshold,
                    std::uint64_t start, std::vector<std::uint64_t>* offsets,
                    PoolScaleResult* result) {
  std::map<std::uint64_t, std::vector<std::size_t>, std::greater<>> classes;
  for (std::size_t node : small_nodes) {
    classes[HarmonicUpper(search.placement.nodes[node].size, threshold)].push_back(node);
  }

  std::uint64_t top = start;
  for (auto& [upper, nodes] : classes) {
    std::uint64_t class_alignment = 1;
    for (std::size_t node : nodes) {
      const auto merged =
          LeastCommonMultiple(class_alignment, search.placement.nodes[node].alignment);
      if (!merged.has_value()) {
        result->diagnostic = "scale_separated_grid_dp small-band alignment overflows";
        return false;
      }
      class_alignment = merged.value();
    }
    const std::optional<std::uint64_t> track_size = detail::AlignUp(upper, class_alignment);
    const std::optional<std::uint64_t> base = detail::AlignUp(top, class_alignment);
    if (!track_size.has_value() || !base.has_value()) {
      result->diagnostic = "scale_separated_grid_dp small-band address overflows";
      return false;
    }

    std::sort(nodes.begin(), nodes.end(), [&](std::size_t first, std::size_t second) {
      const auto first_hull = LifetimeHull(problem, search.placement.nodes[first]);
      const auto second_hull = LifetimeHull(problem, search.placement.nodes[second]);
      if (!first_hull.has_value() || !second_hull.has_value()) {
        return first < second;
      }
      return std::tie(first_hull->first, search.placement.nodes[first].representative) <
             std::tie(second_hull->first, search.placement.nodes[second].representative);
    });

    std::map<std::size_t, std::uint32_t> color_by_node;
    std::uint32_t color_count = 0;
    for (std::size_t node : nodes) {
      std::set<std::uint32_t> blocked;
      for (std::size_t neighbor : search.hard_neighbors[node]) {
        const auto found = color_by_node.find(neighbor);
        if (found != color_by_node.end()) {
          blocked.insert(found->second);
        }
      }
      std::uint32_t color = 0;
      while (blocked.count(color) != 0) {
        ++color;
      }
      color_by_node.emplace(node, color);
      color_count = std::max(color_count, static_cast<std::uint32_t>(color + 1));
      if (color != 0 && track_size.value() > std::numeric_limits<std::uint64_t>::max() / color) {
        result->diagnostic = "scale_separated_grid_dp small-band address overflows";
        return false;
      }
      const std::uint64_t displacement = static_cast<std::uint64_t>(color) * track_size.value();
      if (detail::AddOverflows(base.value(), displacement)) {
        result->diagnostic = "scale_separated_grid_dp small-band address overflows";
        return false;
      }
      (*offsets)[node] = base.value() + displacement;
    }
    if (color_count != 0 &&
        track_size.value() >
            (std::numeric_limits<std::uint64_t>::max() - base.value()) / color_count) {
      result->diagnostic = "scale_separated_grid_dp small-band height overflows";
      return false;
    }
    top = base.value() + static_cast<std::uint64_t>(color_count) * track_size.value();
    result->small_tracks =
        detail::SaturatingAdd(result->small_tracks, static_cast<std::uint64_t>(color_count));
  }
  result->small_band_height = top - start;
  return true;
}

PoolScaleResult SolvePool(const DsaProblem& problem, const detail::ReusePenaltySearchSpace& search,
                          const Pool& pool, const ScaleSeparatedGridDpOptions& options) {
  PoolScaleResult result;
  result.offsets.assign(search.placement.nodes.size(), 0);
  if (!pool.capacity.has_value()) {
    result.diagnostic = "scale_separated_grid_dp requires fixed pool capacities";
    return result;
  }
  if (!pool.reserved_ranges.empty()) {
    result.diagnostic = "scale_separated_grid_dp does not support reserved ranges";
    return result;
  }
  const std::vector<std::size_t> pool_nodes = NodesInPool(search, pool.id);
  if (pool_nodes.empty()) {
    result.status = SolveStatus::kFeasible;
    return result;
  }

  const std::optional<std::uint64_t> minimum_soft_size = MinimumSoftEndpointSize(search, pool.id);
  if (!minimum_soft_size.has_value()) {
    result.status = SolveStatus::kUnsupported;
    result.diagnostic = "scale_separated_grid_dp pool has no soft edges";
    return result;
  }
  const std::uint64_t capacity = pool.capacity.value();
  const std::uint64_t minimum_large_size = minimum_soft_size.value();
  if (minimum_large_size < CeilDivide(capacity, options.max_large_fraction_denominator)) {
    result.diagnostic =
        "scale_separated_grid_dp soft endpoints violate the configured scale separation";
    return result;
  }

  std::vector<std::size_t> large_nodes;
  std::vector<std::size_t> small_nodes;
  for (std::size_t node : pool_nodes) {
    if (search.placement.nodes[node].size >= minimum_large_size) {
      large_nodes.push_back(node);
    } else {
      small_nodes.push_back(node);
    }
  }
  result.large_nodes = large_nodes.size();
  result.small_nodes = small_nodes.size();
  if (!IsIntervalSmallBand(problem, search, small_nodes)) {
    result.diagnostic =
        "scale_separated_grid_dp harmonic band requires singleton, "
        "single-interval small buffers with only temporal hard conflicts";
    return result;
  }

  std::uint64_t grid_alignment = 1;
  for (std::size_t node : large_nodes) {
    const auto merged = LeastCommonMultiple(grid_alignment, search.placement.nodes[node].alignment);
    if (!merged.has_value()) {
      result.diagnostic = "scale_separated_grid_dp grid alignment overflows";
      return result;
    }
    grid_alignment = merged.value();
  }
  const std::uint64_t raw_quantum = minimum_large_size / options.epsilon_denominator;
  const std::uint64_t quantum = raw_quantum / grid_alignment * grid_alignment;
  if (quantum == 0) {
    result.diagnostic = "scale_separated_grid_dp cannot form an alignment-compatible grid quantum";
    return result;
  }
  result.grid_quantum = quantum;
  const std::uint64_t augmentation = CeilDivide(capacity, options.epsilon_denominator);
  if (detail::AddOverflows(capacity, augmentation)) {
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostic = "scale_separated_grid_dp augmented capacity overflows";
    return result;
  }
  result.large_band_limit = capacity + augmentation;

  std::vector<TimedNode> timed;
  timed.reserve(large_nodes.size());
  for (std::size_t node : large_nodes) {
    const auto hull = LifetimeHull(problem, search.placement.nodes[node]);
    const auto rounded = detail::AlignUp(search.placement.nodes[node].size, quantum);
    if (!hull.has_value() || !rounded.has_value()) {
      result.status = SolveStatus::kInvalidProblem;
      result.diagnostic = "scale_separated_grid_dp cannot normalize a large buffer";
      return result;
    }
    timed.push_back({node, hull->first, hull->second, rounded.value()});
  }
  std::sort(timed.begin(), timed.end(), [&](const TimedNode& first, const TimedNode& second) {
    return std::tie(first.birth, first.death, search.placement.nodes[first.node].representative) <
           std::tie(second.birth, second.death, search.placement.nodes[second.node].representative);
  });
  std::vector<std::size_t> rank_by_node(search.placement.nodes.size(), kNoIndex);
  for (std::size_t rank = 0; rank < timed.size(); ++rank) {
    rank_by_node[timed[rank].node] = rank;
  }

  result.status = SolveStatus::kUnsupported;
  const std::optional<std::vector<std::uint32_t>> colors =
      RunGridDp(search, timed, rank_by_node, quantum, result.large_band_limit, options, &result);
  if (!colors.has_value()) {
    return result;
  }
  for (std::size_t rank = 0; rank < timed.size(); ++rank) {
    result.offsets[timed[rank].node] = static_cast<std::uint64_t>(colors->at(rank)) * quantum;
    result.large_peak =
        std::max(result.large_peak,
                 result.offsets[timed[rank].node] + search.placement.nodes[timed[rank].node].size);
  }
  if (!PlaceSmallBand(problem, search, small_nodes, minimum_large_size, result.large_peak,
                      &result.offsets, &result)) {
    result.status = SolveStatus::kInvalidProblem;
    return result;
  }
  result.status = SolveStatus::kFeasible;
  return result;
}

DsaResult BuildBicriteriaResult(const DsaProblem& problem,
                                const detail::ReusePenaltySearchSpace& search,
                                const std::vector<std::uint64_t>& offsets) {
  DsaResult result;
  DsaSolution solution = detail::ExpandReuseNodeSolution(search, offsets);
  DsaProblem without_capacities = problem;
  for (Pool& pool : without_capacities.pools) {
    pool.capacity.reset();
  }
  const std::vector<std::string> errors = ValidateSolution(without_capacities, solution);
  if (!errors.empty()) {
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics.emplace_back(
        "scale_separated_grid_dp produced a structurally invalid placement");
    result.diagnostics.insert(result.diagnostics.end(), errors.begin(), errors.end());
    return result;
  }
  result.objective = EvaluateObjective(problem, solution);
  result.status =
      EvaluateObjectiveMetric(problem, result.objective, ObjectiveMetric::kCapacityOverflow) == 0
          ? SolveStatus::kFeasible
          : SolveStatus::kBestEffortNoFit;
  result.solution = std::move(solution);
  return result;
}

}  // namespace

ScaleSeparatedGridDpSolver::ScaleSeparatedGridDpSolver(ScaleSeparatedGridDpOptions options)
    : options_(options) {}

const char* ScaleSeparatedGridDpSolver::Name() const noexcept { return "scale_separated_grid_dp"; }

SolverCapabilities ScaleSeparatedGridDpSolver::Capabilities() const noexcept {
  return ScaleCapabilities();
}

DsaResult ScaleSeparatedGridDpSolver::Solve(const DsaProblem& problem) const {
  const std::vector<std::string> problem_errors = ValidateProblem(problem);
  if (!problem_errors.empty()) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = problem_errors;
    return result;
  }
  if (options_.max_large_fraction_denominator == 0 || options_.epsilon_denominator == 0 ||
      options_.max_grid_offsets == 0) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics.emplace_back(
        "scale_separated_grid_dp option denominators and grid guard must be nonzero");
    return result;
  }
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    DsaResult result;
    result.status = SolveStatus::kUnsupported;
    for (const std::string& feature : compatibility.unsupported_features) {
      result.diagnostics.emplace_back(std::string(Name()) + " does not support feature '" +
                                      feature + "'");
    }
    for (const std::string& objective : compatibility.unsupported_objectives) {
      result.diagnostics.emplace_back(std::string(Name()) + " does not support objective '" +
                                      objective + "'");
    }
    return result;
  }
  if (const auto limitation = ObjectiveLimitation(problem); limitation.has_value()) {
    DsaResult result;
    result.status = SolveStatus::kUnsupported;
    result.diagnostics.push_back(limitation.value());
    return result;
  }

  const detail::ReusePenaltySearchSpace search = detail::BuildReusePenaltySearchSpace(problem);
  if (!search.placement.errors.empty()) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = search.placement.errors;
    return result;
  }
  if (search.placement.flexible_pools) {
    DsaResult result;
    result.status = SolveStatus::kUnsupported;
    result.diagnostics.emplace_back(
        "scale_separated_grid_dp does not support flexible pool assignment");
    return result;
  }
  if (search.soft_weights.empty()) {
    DsaResult result;
    result.status = SolveStatus::kUnsupported;
    result.diagnostics.emplace_back(
        "scale_separated_grid_dp requires at least one soft reuse edge");
    return result;
  }

  const DsaResult baseline =
      detail::PlaceWithOrder(problem, detail::DefaultPlacementOrder(problem));
  if (!baseline.solution.has_value()) {
    DsaResult result;
    result.status = baseline.status;
    result.diagnostics = baseline.diagnostics;
    return result;
  }
  std::vector<std::uint64_t> offsets(search.placement.nodes.size(), 0);
  for (std::size_t node = 0; node < search.placement.nodes.size(); ++node) {
    const Placement* placement =
        baseline.solution->Find(search.placement.nodes[node].representative);
    if (placement == nullptr) {
      DsaResult result;
      result.status = SolveStatus::kInvalidProblem;
      result.diagnostics.emplace_back("scale_separated_grid_dp baseline omitted a normalized node");
      return result;
    }
    offsets[node] = placement->offset;
  }

  std::uint64_t rounded_cost = 0;
  std::uint64_t grid_quantum = 0;
  std::uint64_t large_band_limit = 0;
  std::uint64_t small_band_height = 0;
  std::uint64_t dp_states = 0;
  std::uint64_t dp_transitions = 0;
  std::uint64_t small_tracks = 0;
  std::uint64_t large_nodes = 0;
  std::uint64_t small_nodes = 0;
  std::uint64_t max_window = 0;
  std::uint64_t max_soft_span = 0;
  std::uint64_t max_grid_offsets = 0;
  std::uint64_t optimized_pools = 0;
  for (const Pool& pool : problem.pools) {
    if (!MinimumSoftEndpointSize(search, pool.id).has_value()) {
      continue;
    }
    const PoolScaleResult pool_result = SolvePool(problem, search, pool, options_);
    if (pool_result.status != SolveStatus::kFeasible) {
      DsaResult result;
      result.status = pool_result.status;
      if (!pool_result.diagnostic.empty()) {
        result.diagnostics.push_back(pool_result.diagnostic);
      }
      return result;
    }
    ++optimized_pools;
    rounded_cost = detail::SaturatingAdd(rounded_cost, pool_result.rounded_cost);
    grid_quantum = std::max(grid_quantum, pool_result.grid_quantum);
    large_band_limit = std::max(large_band_limit, pool_result.large_band_limit);
    small_band_height = detail::SaturatingAdd(small_band_height, pool_result.small_band_height);
    dp_states = detail::SaturatingAdd(dp_states, pool_result.dp_states);
    dp_transitions = detail::SaturatingAdd(dp_transitions, pool_result.dp_transitions);
    small_tracks = detail::SaturatingAdd(small_tracks, pool_result.small_tracks);
    large_nodes =
        detail::SaturatingAdd(large_nodes, static_cast<std::uint64_t>(pool_result.large_nodes));
    small_nodes =
        detail::SaturatingAdd(small_nodes, static_cast<std::uint64_t>(pool_result.small_nodes));
    max_window = std::max(max_window, static_cast<std::uint64_t>(pool_result.max_window));
    max_soft_span = std::max(max_soft_span, static_cast<std::uint64_t>(pool_result.max_soft_span));
    max_grid_offsets =
        std::max(max_grid_offsets, static_cast<std::uint64_t>(pool_result.max_grid_offsets));
    for (std::size_t node = 0; node < offsets.size(); ++node) {
      if (search.placement.nodes[node].pool == pool.id) {
        offsets[node] = pool_result.offsets[node];
      }
    }
  }

  DsaResult result = BuildBicriteriaResult(problem, search, offsets);
  if (result.solution.has_value() && result.objective.reuse_cost > rounded_cost) {
    result.status = SolveStatus::kInvalidProblem;
    result.solution.reset();
    result.diagnostics.emplace_back(
        "scale_separated_grid_dp true cost exceeds its rounded DP cost");
    return result;
  }
  result.solver_metrics = {
      {"optimized_pools", optimized_pools},
      {"large_nodes", large_nodes},
      {"small_nodes", small_nodes},
      {"grid_quantum_max", grid_quantum},
      {"large_band_limit_max", large_band_limit},
      {"small_band_bytes_total", small_band_height},
      {"dp_states", dp_states},
      {"dp_transitions", dp_transitions},
      {"max_window_nodes", max_window},
      {"max_soft_span", max_soft_span},
      {"max_grid_offsets", max_grid_offsets},
      {"rounded_dp_cost", rounded_cost},
      {"epsilon_denominator", options_.epsilon_denominator},
      {"scale_denominator_bound", options_.max_large_fraction_denominator},
      {"small_tracks", small_tracks},
      {"bicriteria_height_augmentation", 1},
  };
  result.diagnostics.emplace_back(
      "scale_separated_grid_dp minimizes rounded large-buffer reuse cost "
      "and stacks a soft-edge-free harmonic small-buffer band; "
      "BestEffortNoFit denotes resource augmentation, not structural "
      "invalidity");
  return result;
}

}  // namespace dsa

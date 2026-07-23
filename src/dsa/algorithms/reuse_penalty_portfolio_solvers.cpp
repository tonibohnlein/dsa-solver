// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/reuse_penalty_portfolio_solvers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "dsa/algorithms/placement_engine.h"
#include "dsa/algorithms/reuse_penalty_search.h"
#include "dsa/algorithms/reuse_penalty_treewidth_solver.h"
#include "dsa/algorithms/solver.h"
#include "dsa/model/model.h"
#include "dsa/model/validator.h"

namespace dsa {
namespace {

using Score = std::vector<std::uint64_t>;

SolverCapabilities CapacityTwoCapabilities() {
  SolverCapabilities capabilities;
  capabilities.multi_interval = true;
  capabilities.reuse_cost = true;
  capabilities.colocations = true;
  capabilities.separations = true;
  capabilities.temporal_exclusions = true;
  capabilities.multi_pool = true;
  capabilities.lexicographic_objective = true;
  capabilities.capacity_objective = true;
  capabilities.peak_objective = false;
  return capabilities;
}

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

Score SolutionScore(const DsaProblem& problem, const DsaSolution& solution) {
  const ObjectiveValue objective = EvaluateObjective(problem, solution);
  Score score;
  score.reserve(problem.objective.terms.size());
  for (ObjectiveMetric metric : problem.objective.terms) {
    score.push_back(EvaluateObjectiveMetric(problem, objective, metric));
  }
  return score;
}

std::optional<std::string> ReuseBeforePeakLimitation(const DsaProblem& problem,
                                                     const char* solver_name) {
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
    return std::string(solver_name) + " requires reuse_cost in the objective";
  }
  if (peak_index.has_value() && peak_index.value() < reuse_index.value()) {
    return std::string(solver_name) + " requires reuse_cost before peak metrics";
  }
  return std::nullopt;
}

struct ComponentEdge {
  std::size_t first_component = 0;
  std::size_t second_component = 0;
  bool equal_flips_activate = false;
  std::uint64_t weight = 0;
};

struct PoolTwoResult {
  SolveStatus status = SolveStatus::kUnsupported;
  std::vector<std::uint8_t> colors;
  std::uint64_t search_nodes = 0;
  std::uint64_t bound_prunes = 0;
};

class PoolTwoSearch {
 public:
  PoolTwoSearch(const DsaProblem& problem, const detail::ReusePenaltySearchSpace& search,
                PoolId pool, std::uint64_t unit, std::uint64_t node_limit)
      : problem_(problem),
        search_(search),
        pool_(pool),
        unit_(unit),
        node_limit_(node_limit),
        component_(search.placement.nodes.size(), std::numeric_limits<std::size_t>::max()),
        parity_(search.placement.nodes.size(), 0),
        best_colors_(search.placement.nodes.size(), 0) {
    for (std::size_t node = 0; node < search_.placement.nodes.size(); ++node) {
      if (search_.placement.nodes[node].pool == pool_) {
        nodes_.push_back(node);
      }
    }
  }

  PoolTwoResult Run() {
    if (!BuildComponents()) {
      return {SolveStatus::kInfeasibleProven, {}, 0, 0};
    }
    BuildSoftObjective();
    flips_.assign(component_count_, -1);
    component_order_.resize(component_count_);
    for (std::size_t component = 0; component < component_count_; ++component) {
      component_order_[component] = component;
    }
    std::sort(component_order_.begin(), component_order_.end(),
              [&](std::size_t first, std::size_t second) {
                if (component_degree_[first] != component_degree_[second]) {
                  return component_degree_[first] > component_degree_[second];
                }
                return first < second;
              });
    best_score_.assign(problem_.objective.terms.size(), std::numeric_limits<std::uint64_t>::max());
    Search(0, fixed_cost_);

    PoolTwoResult result;
    result.search_nodes = search_nodes_;
    result.bound_prunes = bound_prunes_;
    if (limit_hit_) {
      result.status = SolveStatus::kTimeout;
    } else if (!found_) {
      result.status = SolveStatus::kInfeasibleProven;
    } else {
      result.status = SolveStatus::kFeasible;
    }
    if (found_) {
      result.colors = best_colors_;
    }
    return result;
  }

 private:
  bool BuildComponents() {
    for (std::size_t start : nodes_) {
      if (component_[start] != std::numeric_limits<std::size_t>::max()) {
        continue;
      }
      const std::size_t current_component = component_count_++;
      component_[start] = current_component;
      parity_[start] = 0;
      std::deque<std::size_t> queue{start};
      while (!queue.empty()) {
        const std::size_t node = queue.front();
        queue.pop_front();
        for (std::size_t neighbor : search_.hard_neighbors[node]) {
          if (search_.placement.nodes[neighbor].pool != pool_) {
            continue;
          }
          if (component_[neighbor] == std::numeric_limits<std::size_t>::max()) {
            component_[neighbor] = current_component;
            parity_[neighbor] = static_cast<std::uint8_t>(1U - parity_[node]);
            queue.push_back(neighbor);
          } else if (parity_[neighbor] == parity_[node]) {
            return false;
          }
        }
      }
    }
    return true;
  }

  void BuildSoftObjective() {
    component_degree_.assign(component_count_, 0);
    for (const auto& [pair, weight] : search_.soft_weights) {
      if (search_.placement.nodes[pair.first].pool != pool_) {
        continue;
      }
      const std::size_t first_component = component_[pair.first];
      const std::size_t second_component = component_[pair.second];
      const bool equal_parity = parity_[pair.first] == parity_[pair.second];
      if (first_component == second_component) {
        if (equal_parity) {
          fixed_cost_ = detail::SaturatingAdd(fixed_cost_, weight);
        }
        continue;
      }
      edges_.push_back({first_component, second_component, equal_parity, weight});
      ++component_degree_[first_component];
      ++component_degree_[second_component];
    }
  }

  [[nodiscard]] std::uint64_t IncrementalCost(std::size_t component, std::int8_t flip) const {
    std::uint64_t cost = 0;
    for (const ComponentEdge& edge : edges_) {
      std::size_t other = std::numeric_limits<std::size_t>::max();
      if (edge.first_component == component) {
        other = edge.second_component;
      } else if (edge.second_component == component) {
        other = edge.first_component;
      } else {
        continue;
      }
      if (flips_[other] < 0) {
        continue;
      }
      const bool flips_equal = flip == flips_[other];
      if (flips_equal == edge.equal_flips_activate) {
        cost = detail::SaturatingAdd(cost, edge.weight);
      }
    }
    return cost;
  }

  void Search(std::size_t depth, std::uint64_t current_cost) {
    if (limit_hit_) {
      return;
    }
    if (node_limit_ != 0 && search_nodes_ >= node_limit_) {
      limit_hit_ = true;
      return;
    }
    ++search_nodes_;
    const Score lower_bound = PoolScore(problem_, current_cost, 0);
    if (found_ && lower_bound >= best_score_) {
      ++bound_prunes_;
      return;
    }
    if (depth == component_order_.size()) {
      std::uint64_t peak = 0;
      std::vector<std::uint8_t> colors(search_.placement.nodes.size(), 0);
      for (std::size_t node : nodes_) {
        colors[node] = static_cast<std::uint8_t>(parity_[node] ^ flips_[component_[node]]);
        peak = std::max(peak, (static_cast<std::uint64_t>(colors[node]) + 1) * unit_);
      }
      const Score score = PoolScore(problem_, current_cost, peak);
      if (!found_ || score < best_score_) {
        found_ = true;
        best_score_ = score;
        best_colors_ = std::move(colors);
      }
      return;
    }

    const std::size_t component = component_order_[depth];
    const std::int8_t last_flip = depth == 0 ? 0 : 1;
    for (std::int8_t flip = 0; flip <= last_flip; ++flip) {
      const std::uint64_t incremental = IncrementalCost(component, flip);
      flips_[component] = flip;
      Search(depth + 1, detail::SaturatingAdd(current_cost, incremental));
      flips_[component] = -1;
      if (limit_hit_) {
        return;
      }
    }
  }

  const DsaProblem& problem_;
  const detail::ReusePenaltySearchSpace& search_;
  PoolId pool_;
  std::uint64_t unit_;
  std::uint64_t node_limit_;
  std::vector<std::size_t> nodes_;
  std::vector<std::size_t> component_;
  std::vector<std::uint8_t> parity_;
  std::size_t component_count_ = 0;
  std::vector<ComponentEdge> edges_;
  std::vector<std::size_t> component_degree_;
  std::uint64_t fixed_cost_ = 0;
  std::vector<std::size_t> component_order_;
  std::vector<std::int8_t> flips_;
  std::vector<std::uint8_t> best_colors_;
  Score best_score_;
  bool found_ = false;
  bool limit_hit_ = false;
  std::uint64_t search_nodes_ = 0;
  std::uint64_t bound_prunes_ = 0;
};

std::optional<std::string> ValidateCapacityTwoShape(const DsaProblem& problem,
                                                    const detail::ReusePenaltySearchSpace& search) {
  for (const Pool& pool : problem.pools) {
    if (!pool.capacity.has_value()) {
      return "capacity_two_exact requires fixed capacities";
    }
    const std::uint64_t capacity = pool.capacity.value_or(0);
    if (!pool.reserved_ranges.empty()) {
      return "capacity_two_exact does not support reserved ranges";
    }
    std::optional<std::uint64_t> unit;
    for (const detail::PlacementSearchNode& node : search.placement.nodes) {
      if (node.pool != pool.id) {
        continue;
      }
      if (!unit.has_value()) {
        unit = node.size;
      }
      if (node.size != unit.value() || unit.value() % node.alignment != 0) {
        return "capacity_two_exact requires uniform unit sizes and compatible "
               "alignment";
      }
    }
    if (unit.has_value()) {
      if (unit.value() == 0 || capacity / unit.value() != 2) {
        return "capacity_two_exact requires exactly two usable unit addresses";
      }
    }
  }
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
  if (reuse_index.has_value() && peak_index.has_value() &&
      peak_index.value() < reuse_index.value()) {
    return "capacity_two_exact requires reuse_cost before peak metrics";
  }
  return std::nullopt;
}

}  // namespace

CapacityTwoExactSolver::CapacityTwoExactSolver(CapacityTwoExactOptions options)
    : options_(options) {}

const char* CapacityTwoExactSolver::Name() const noexcept { return "capacity_two_exact"; }

SolverCapabilities CapacityTwoExactSolver::Capabilities() const noexcept {
  return CapacityTwoCapabilities();
}

DsaResult CapacityTwoExactSolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    DsaResult result;
    const std::vector<std::string> validation = ValidateProblem(problem);
    if (!validation.empty()) {
      result.status = SolveStatus::kInvalidProblem;
      result.diagnostics = validation;
      return result;
    }
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

  const detail::ReusePenaltySearchSpace search = detail::BuildReusePenaltySearchSpace(problem);
  if (!search.placement.errors.empty()) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = search.placement.errors;
    return result;
  }
  if (const std::optional<std::string> limitation = ValidateCapacityTwoShape(problem, search);
      limitation.has_value()) {
    DsaResult result;
    result.status = SolveStatus::kUnsupported;
    result.diagnostics.push_back(limitation.value());
    return result;
  }

  std::vector<std::uint64_t> offsets(search.placement.nodes.size(), 0);
  std::uint64_t search_nodes = 0;
  std::uint64_t bound_prunes = 0;
  bool any_timeout = false;
  for (const Pool& pool : problem.pools) {
    std::optional<std::uint64_t> unit;
    for (const detail::PlacementSearchNode& node : search.placement.nodes) {
      if (node.pool == pool.id) {
        unit = node.size;
        break;
      }
    }
    if (!unit.has_value()) {
      continue;
    }
    const PoolTwoResult pool_result =
        PoolTwoSearch(problem, search, pool.id, unit.value(), options_.max_search_nodes).Run();
    search_nodes = detail::SaturatingAdd(search_nodes, pool_result.search_nodes);
    bound_prunes = detail::SaturatingAdd(bound_prunes, pool_result.bound_prunes);
    if (pool_result.status == SolveStatus::kTimeout && !pool_result.colors.empty()) {
      any_timeout = true;
    } else if (pool_result.status != SolveStatus::kFeasible) {
      DsaResult result;
      result.status = pool_result.status;
      result.solver_metrics = {
          {"search_nodes", search_nodes},
          {"bound_prunes", bound_prunes},
          {"optimality_proven", pool_result.status == SolveStatus::kInfeasibleProven ? 1U : 0U},
      };
      return result;
    }
    for (std::size_t node = 0; node < offsets.size(); ++node) {
      if (search.placement.nodes[node].pool == pool.id) {
        offsets[node] = static_cast<std::uint64_t>(pool_result.colors[node]) * unit.value();
      }
    }
  }

  DsaResult result = detail::BuildValidatedReuseResult(
      problem, search, offsets, any_timeout ? SolveStatus::kTimeout : SolveStatus::kFeasible);
  result.solver_metrics = {
      {"search_nodes", search_nodes},
      {"bound_prunes", bound_prunes},
      {"optimality_proven", any_timeout ? 0U : 1U},
  };
  result.diagnostics.emplace_back(
      any_timeout ? "capacity_two_exact reached its per-pool node limit and retained "
                    "the best complete incumbent"
                  : "capacity_two_exact proved the optimum by hard-component "
                    "bipartition and weighted flip enumeration");
  return result;
}

namespace {

struct FlowArc {
  int to = 0;
  int reverse = 0;
  int capacity = 0;
  std::int64_t cost = 0;
  bool matching_arc = false;
  std::size_t right = 0;
};

void AddFlowArc(std::vector<std::vector<FlowArc>>* graph, int from, int to, int capacity,
                std::int64_t cost, bool matching_arc = false, std::size_t right = 0) {
  const std::size_t from_index = static_cast<std::size_t>(from);
  const std::size_t to_index = static_cast<std::size_t>(to);
  const int forward_reverse = static_cast<int>((*graph)[to_index].size());
  const int reverse_reverse = static_cast<int>((*graph)[from_index].size());
  (*graph)[from_index].push_back({to, forward_reverse, capacity, cost, matching_arc, right});
  (*graph)[to_index].push_back({from, reverse_reverse, 0, -cost, false, 0});
}

std::optional<std::vector<std::pair<std::size_t, std::size_t>>> MinimumCardinalityMatching(
    const std::vector<std::vector<std::uint64_t>>& costs, std::size_t cardinality) {
  const std::size_t left_count = costs.size();
  const std::size_t right_count = left_count == 0 ? 0 : costs.front().size();
  if (cardinality == 0) {
    return std::vector<std::pair<std::size_t, std::size_t>>{};
  }
  if (left_count > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      right_count > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      cardinality > std::min(left_count, right_count)) {
    return std::nullopt;
  }

  constexpr std::int64_t kInfinity = std::numeric_limits<std::int64_t>::max() / 4;
  std::uint64_t total_weight = 0;
  for (const auto& row : costs) {
    if (row.size() != right_count) {
      return std::nullopt;
    }
    for (std::uint64_t cost : row) {
      if (cost > static_cast<std::uint64_t>(kInfinity)) {
        return std::nullopt;
      }
      total_weight = detail::SaturatingAdd(total_weight, cost);
    }
  }
  if (total_weight > static_cast<std::uint64_t>(kInfinity)) {
    return std::nullopt;
  }

  const int source = 0;
  const int left_begin = 1;
  const int right_begin = left_begin + static_cast<int>(left_count);
  const int sink = right_begin + static_cast<int>(right_count);
  std::vector<std::vector<FlowArc>> graph(static_cast<std::size_t>(sink + 1));
  for (std::size_t left = 0; left < left_count; ++left) {
    AddFlowArc(&graph, source, left_begin + static_cast<int>(left), 1, 0);
    for (std::size_t right = 0; right < right_count; ++right) {
      AddFlowArc(&graph, left_begin + static_cast<int>(left), right_begin + static_cast<int>(right),
                 1, static_cast<std::int64_t>(costs[left][right]), true, right);
    }
  }
  for (std::size_t right = 0; right < right_count; ++right) {
    AddFlowArc(&graph, right_begin + static_cast<int>(right), sink, 1, 0);
  }

  for (std::size_t flow = 0; flow < cardinality; ++flow) {
    std::vector<std::int64_t> distance(graph.size(), kInfinity);
    std::vector<int> previous_vertex(graph.size(), -1);
    std::vector<int> previous_arc(graph.size(), -1);
    distance[source] = 0;
    for (std::size_t iteration = 1; iteration < graph.size(); ++iteration) {
      bool changed = false;
      for (std::size_t from = 0; from < graph.size(); ++from) {
        if (distance[from] == kInfinity) {
          continue;
        }
        for (std::size_t arc_index = 0; arc_index < graph[from].size(); ++arc_index) {
          const FlowArc& arc = graph[from][arc_index];
          if (arc.capacity == 0) {
            continue;
          }
          const std::int64_t candidate = distance[from] + arc.cost;
          const std::size_t to = static_cast<std::size_t>(arc.to);
          // Keep the first shortest predecessor. Rewriting equal-distance
          // predecessors in a residual graph with zero-cost arcs can create a
          // predecessor cycle even though the distances are valid.
          if (candidate < distance[to]) {
            distance[to] = candidate;
            previous_vertex[to] = static_cast<int>(from);
            previous_arc[to] = static_cast<int>(arc_index);
            changed = true;
          }
        }
      }
      if (!changed) {
        break;
      }
    }
    if (distance[static_cast<std::size_t>(sink)] == kInfinity) {
      return std::nullopt;
    }
    for (int vertex = sink; vertex != source;) {
      const int from = previous_vertex[static_cast<std::size_t>(vertex)];
      const int arc_index = previous_arc[static_cast<std::size_t>(vertex)];
      if (from < 0 || arc_index < 0) {
        return std::nullopt;
      }
      FlowArc& arc = graph[static_cast<std::size_t>(from)][static_cast<std::size_t>(arc_index)];
      --arc.capacity;
      ++graph[static_cast<std::size_t>(vertex)][static_cast<std::size_t>(arc.reverse)].capacity;
      vertex = from;
    }
  }

  std::vector<std::pair<std::size_t, std::size_t>> matching;
  for (std::size_t left = 0; left < left_count; ++left) {
    const auto& arcs = graph[static_cast<std::size_t>(left_begin) + left];
    for (const FlowArc& arc : arcs) {
      if (arc.matching_arc && arc.capacity == 0) {
        matching.emplace_back(left, arc.right);
      }
    }
  }
  if (matching.size() != cardinality) {
    return std::nullopt;
  }
  return matching;
}

std::int64_t NodeBirth(const DsaProblem& problem, const detail::PlacementSearchNode& node) {
  std::int64_t birth = std::numeric_limits<std::int64_t>::max();
  for (BufferId member : node.members) {
    const Buffer* buffer = problem.FindBuffer(member);
    if (buffer == nullptr) {
      continue;
    }
    for (const Interval& interval : buffer->live_intervals) {
      birth = std::min(birth, interval.lower);
    }
  }
  return birth;
}

struct SpanPool {
  SolveStatus status = SolveStatus::kUnsupported;
  std::string diagnostic;
  std::uint64_t unit = 0;
  std::uint64_t colors = 0;
  std::vector<std::vector<std::size_t>> phases;
  std::vector<std::size_t> phase_by_node;
};

SpanPool BuildSpanPool(const DsaProblem& problem, const detail::ReusePenaltySearchSpace& search,
                       PoolId pool_id, std::size_t max_phase_buffers) {
  SpanPool result;
  const Pool* pool = problem.FindPool(pool_id);
  if (pool == nullptr || !pool->capacity.has_value()) {
    result.diagnostic = "span_one_min_cost_flow requires fixed capacities";
    return result;
  }
  if (!pool->reserved_ranges.empty()) {
    result.diagnostic = "span_one_min_cost_flow does not support reserved ranges";
    return result;
  }

  std::vector<std::size_t> nodes;
  for (std::size_t node = 0; node < search.placement.nodes.size(); ++node) {
    if (search.placement.nodes[node].pool != pool_id) {
      continue;
    }
    nodes.push_back(node);
    if (result.unit == 0) {
      result.unit = search.placement.nodes[node].size;
    }
    if (search.placement.nodes[node].size != result.unit ||
        result.unit % search.placement.nodes[node].alignment != 0) {
      result.diagnostic =
          "span_one_min_cost_flow requires uniform unit sizes and compatible "
          "alignment";
      return result;
    }
  }
  if (nodes.empty()) {
    result.status = SolveStatus::kFeasible;
    result.phase_by_node.assign(search.placement.nodes.size(),
                                std::numeric_limits<std::size_t>::max());
    return result;
  }
  result.colors = pool->capacity.value() / result.unit;
  result.phase_by_node.assign(search.placement.nodes.size(),
                              std::numeric_limits<std::size_t>::max());

  std::vector<bool> visited(search.placement.nodes.size(), false);
  for (std::size_t start : nodes) {
    if (visited[start]) {
      continue;
    }
    std::vector<std::size_t> component;
    std::deque<std::size_t> queue{start};
    visited[start] = true;
    while (!queue.empty()) {
      const std::size_t current = queue.front();
      queue.pop_front();
      component.push_back(current);
      for (std::size_t neighbor : search.hard_neighbors[current]) {
        if (search.placement.nodes[neighbor].pool == pool_id && !visited[neighbor]) {
          visited[neighbor] = true;
          queue.push_back(neighbor);
        }
      }
    }
    for (std::size_t first = 0; first < component.size(); ++first) {
      for (std::size_t second = first + 1; second < component.size(); ++second) {
        if (search.hard_neighbors[component[first]].count(component[second]) == 0) {
          result.diagnostic =
              "span_one_min_cost_flow requires every hard component to be a "
              "clique";
          return result;
        }
      }
    }
    if (component.size() > result.colors) {
      result.status = SolveStatus::kInfeasibleProven;
      result.diagnostic = "a span-one phase contains more live buffers than capacity";
      return result;
    }
    if (component.size() > max_phase_buffers) {
      result.diagnostic = "span_one_min_cost_flow phase exceeds max_phase_buffers";
      return result;
    }
    std::sort(component.begin(), component.end(), [&](std::size_t first, std::size_t second) {
      return search.placement.nodes[first].representative <
             search.placement.nodes[second].representative;
    });
    result.phases.push_back(std::move(component));
  }
  std::sort(result.phases.begin(), result.phases.end(),
            [&](const std::vector<std::size_t>& first, const std::vector<std::size_t>& second) {
              const std::int64_t first_birth =
                  NodeBirth(problem, search.placement.nodes[first.front()]);
              const std::int64_t second_birth =
                  NodeBirth(problem, search.placement.nodes[second.front()]);
              if (first_birth != second_birth) {
                return first_birth < second_birth;
              }
              return search.placement.nodes[first.front()].representative <
                     search.placement.nodes[second.front()].representative;
            });
  for (std::size_t phase = 0; phase < result.phases.size(); ++phase) {
    for (std::size_t node : result.phases[phase]) {
      result.phase_by_node[node] = phase;
    }
  }
  for (const auto& [pair, weight] : search.soft_weights) {
    static_cast<void>(weight);
    if (search.placement.nodes[pair.first].pool != pool_id) {
      continue;
    }
    const std::size_t first_phase = result.phase_by_node[pair.first];
    const std::size_t second_phase = result.phase_by_node[pair.second];
    const std::size_t distance =
        first_phase > second_phase ? first_phase - second_phase : second_phase - first_phase;
    if (distance != 1) {
      result.diagnostic =
          "span_one_min_cost_flow requires soft edges between adjacent "
          "phases";
      return result;
    }
  }
  result.status = SolveStatus::kFeasible;
  return result;
}

std::optional<std::vector<std::uint64_t>> SolveSpanPool(
    const detail::ReusePenaltySearchSpace& search, const SpanPool& span) {
  std::vector<std::uint64_t> colors(search.placement.nodes.size(), 0);
  if (span.phases.empty()) {
    return colors;
  }
  for (std::size_t index = 0; index < span.phases.front().size(); ++index) {
    colors[span.phases.front()[index]] = index;
  }

  for (std::size_t phase = 0; phase + 1 < span.phases.size(); ++phase) {
    const std::vector<std::size_t>& left = span.phases[phase];
    const std::vector<std::size_t>& right = span.phases[phase + 1];
    const std::size_t required =
        left.size() + right.size() > span.colors
            ? left.size() + right.size() - static_cast<std::size_t>(span.colors)
            : 0;
    std::vector<std::vector<std::uint64_t>> costs(left.size(),
                                                  std::vector<std::uint64_t>(right.size(), 0));
    for (std::size_t first = 0; first < left.size(); ++first) {
      for (std::size_t second = 0; second < right.size(); ++second) {
        const auto found =
            search.soft_weights.find(detail::CanonicalNodePair(left[first], right[second]));
        if (found != search.soft_weights.end()) {
          costs[first][second] = found->second;
        }
      }
    }
    const auto matching = MinimumCardinalityMatching(costs, required);
    if (!matching.has_value()) {
      return std::nullopt;
    }
    std::vector<bool> right_assigned(right.size(), false);
    std::set<std::uint64_t> used_left_colors;
    for (std::size_t node : left) {
      used_left_colors.insert(colors[node]);
    }
    for (const auto& [first, second] : matching.value()) {
      colors[right[second]] = colors[left[first]];
      right_assigned[second] = true;
    }
    std::uint64_t next_free = 0;
    for (std::size_t second = 0; second < right.size(); ++second) {
      if (right_assigned[second]) {
        continue;
      }
      while (used_left_colors.count(next_free) != 0) {
        ++next_free;
      }
      if (next_free >= span.colors) {
        return std::nullopt;
      }
      colors[right[second]] = next_free;
      ++next_free;
    }
  }

  std::vector<std::uint64_t> offsets(colors.size(), 0);
  for (std::size_t node = 0; node < colors.size(); ++node) {
    if (span.phase_by_node[node] != std::numeric_limits<std::size_t>::max()) {
      offsets[node] = colors[node] * span.unit;
    }
  }
  return offsets;
}

}  // namespace

SpanOneMinCostFlowSolver::SpanOneMinCostFlowSolver(SpanOneMinCostFlowOptions options)
    : options_(options) {}

const char* SpanOneMinCostFlowSolver::Name() const noexcept { return "span_one_min_cost_flow"; }

SolverCapabilities SpanOneMinCostFlowSolver::Capabilities() const noexcept {
  return CapacityTwoCapabilities();
}

DsaResult SpanOneMinCostFlowSolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    DsaResult result;
    const std::vector<std::string> validation = ValidateProblem(problem);
    if (!validation.empty()) {
      result.status = SolveStatus::kInvalidProblem;
      result.diagnostics = validation;
      return result;
    }
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
  if (const auto limitation = ReuseBeforePeakLimitation(problem, Name()); limitation.has_value()) {
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
  std::vector<std::uint64_t> offsets(search.placement.nodes.size(), 0);
  std::uint64_t phases = 0;
  std::uint64_t boundaries = 0;
  for (const Pool& pool : problem.pools) {
    const SpanPool span = BuildSpanPool(problem, search, pool.id, options_.max_phase_buffers);
    if (span.status != SolveStatus::kFeasible) {
      DsaResult result;
      result.status = span.status;
      result.diagnostics.push_back(span.diagnostic);
      return result;
    }
    const auto pool_offsets = SolveSpanPool(search, span);
    if (!pool_offsets.has_value()) {
      DsaResult result;
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.emplace_back(
          "span_one_min_cost_flow could not represent matching costs safely");
      return result;
    }
    phases = detail::SaturatingAdd(phases, span.phases.size());
    if (!span.phases.empty()) {
      boundaries = detail::SaturatingAdd(boundaries, span.phases.size() - 1);
    }
    for (std::size_t node = 0; node < offsets.size(); ++node) {
      if (search.placement.nodes[node].pool == pool.id) {
        offsets[node] = pool_offsets.value()[node];
      }
    }
  }

  DsaResult result =
      detail::BuildValidatedReuseResult(problem, search, offsets, SolveStatus::kFeasible);
  result.solver_metrics = {
      {"phases", phases},
      {"phase_boundaries", boundaries},
      {"optimal_reuse_cost_proven", 1},
  };
  result.diagnostics.emplace_back(
      "span_one_min_cost_flow proved minimum reuse cost by independent "
      "adjacent-phase cardinality matchings; peak tie-breaking is not "
      "optimized");
  return result;
}

ReusePenaltyPortfolioSolver::ReusePenaltyPortfolioSolver(ReusePenaltyPortfolioOptions options)
    : options_(options) {}

const char* ReusePenaltyPortfolioSolver::Name() const noexcept { return "reuse_penalty_portfolio"; }

SolverCapabilities ReusePenaltyPortfolioSolver::Capabilities() const noexcept {
  return CanonicalBranchAndBoundSolver().Capabilities();
}

DsaResult ReusePenaltyPortfolioSolver::Solve(const DsaProblem& problem) const {
  struct Method {
    const DsaSolver* solver = nullptr;
    std::uint64_t code = 0;
  };
  const SpanOneMinCostFlowSolver span(options_.span_one);
  const CapacityTwoExactSolver capacity_two(options_.capacity_two);
  const TreewidthPartitionDpSolver treewidth(options_.treewidth);
  const CanonicalBranchAndBoundSolver general(options_.general);
  const std::vector<Method> methods = {
      {&span, 1},
      {&capacity_two, 2},
      {&treewidth, 3},
      {&general, 4},
  };
  DsaResult last;
  std::optional<DsaResult> timeout_incumbent;
  Score timeout_score;
  for (const Method& method : methods) {
    DsaResult result = method.solver->Solve(problem);
    if (result.status == SolveStatus::kUnsupported) {
      last = std::move(result);
      continue;
    }
    if (result.status == SolveStatus::kTimeout) {
      if (result.solution.has_value() && ValidateSolution(problem, *result.solution).empty()) {
        result.objective = EvaluateObjective(problem, *result.solution);
        const Score score = SolutionScore(problem, *result.solution);
        if (!timeout_incumbent.has_value() || score < timeout_score) {
          result.solver_metrics["portfolio_method"] = method.code;
          timeout_incumbent = result;
          timeout_score = score;
        }
      }
      last = std::move(result);
      continue;
    }
    result.solver_metrics["portfolio_method"] = method.code;
    result.diagnostics.emplace_back(std::string("reuse_penalty_portfolio selected ") +
                                    method.solver->Name());
    return result;
  }
  if (timeout_incumbent.has_value()) {
    timeout_incumbent->diagnostics.emplace_back(
        "reuse_penalty_portfolio exhausted compatible exact methods and "
        "retained the best timeout incumbent");
    return *timeout_incumbent;
  }
  last.diagnostics.emplace_back("reuse_penalty_portfolio found no compatible exact method");
  return last;
}

}  // namespace dsa

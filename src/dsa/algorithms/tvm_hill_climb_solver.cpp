// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0
//
// Behavioral reimplementation of Apache TVM USMP's hill-climb policy. See
// NOTICE and docs/tvm_hill_climb.md for provenance and deliberate differences.

#include "dsa/algorithms/tvm_hill_climb_solver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dsa/algorithms/placement_engine.h"
#include "dsa/model/validator.h"

namespace dsa {
namespace {

using Score = std::vector<std::uint64_t>;

Score ResultScore(const DsaProblem& problem, const DsaResult& result) {
  if (!result.solution ||
      (result.status != SolveStatus::kFeasible && result.status != SolveStatus::kBestEffortNoFit)) {
    return Score(problem.objective.terms.size(), std::numeric_limits<std::uint64_t>::max());
  }
  Score score;
  score.reserve(problem.objective.terms.size());
  for (ObjectiveMetric metric : problem.objective.terms) {
    score.push_back(EvaluateObjectiveMetric(problem, result.objective, metric));
  }
  return score;
}

using NodeMap = std::unordered_map<BufferId, const detail::PlacementSearchNode*>;
using PositionMap = std::unordered_map<BufferId, std::size_t>;

NodeMap IndexNodes(const detail::PlacementSearchSpace& search_space) {
  NodeMap result;
  result.reserve(search_space.nodes.size());
  for (const detail::PlacementSearchNode& node : search_space.nodes) {
    result.emplace(node.representative, &node);
  }
  return result;
}

PositionMap IndexPositions(const std::vector<BufferId>& order) {
  PositionMap result;
  result.reserve(order.size());
  for (std::size_t index = 0; index < order.size(); ++index) result.emplace(order[index], index);
  return result;
}

std::vector<BufferId> InitialOrder(const detail::PlacementSearchSpace& search_space) {
  std::vector<const detail::PlacementSearchNode*> nodes;
  nodes.reserve(search_space.nodes.size());
  for (const detail::PlacementSearchNode& node : search_space.nodes) nodes.push_back(&node);
  std::sort(nodes.begin(), nodes.end(), [](const auto* first, const auto* second) {
    if (first->pinned != second->pinned) return first->pinned;
    if (first->size != second->size) return first->size > second->size;
    if (first->conflicts.size() != second->conflicts.size()) {
      return first->conflicts.size() > second->conflicts.size();
    }
    if (first->name != second->name) return first->name > second->name;
    return first->representative > second->representative;
  });

  std::vector<BufferId> order;
  order.reserve(nodes.size());
  for (const detail::PlacementSearchNode* node : nodes) order.push_back(node->representative);
  return order;
}

std::size_t RandomIndex(std::mt19937_64* random, std::size_t size) {
  return static_cast<std::size_t>((*random)() % size);
}

bool IsMovable(const NodeMap& nodes, BufferId id) {
  const auto found = nodes.find(id);
  return found != nodes.end() && !found->second->pinned;
}

void SortByPosition(std::vector<BufferId>* ids, const PositionMap& positions) {
  std::sort(ids->begin(), ids->end(), [&](BufferId first, BufferId second) {
    return positions.at(first) < positions.at(second);
  });
}

bool ChooseDistinctSecond(BufferId first, const std::vector<BufferId>& first_level,
                          const std::vector<BufferId>& second_level, std::mt19937_64* random,
                          BufferId* second) {
  const std::size_t attempts = 2 * (first_level.size() + second_level.size());
  for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
    const bool choose_second = !second_level.empty() && ((*random)() % 100 > 25);
    const std::vector<BufferId>& choices = choose_second ? second_level : first_level;
    if (choices.empty()) continue;
    *second = choices[RandomIndex(random, choices.size())];
    if (*second != first) return true;
  }
  for (BufferId candidate : second_level) {
    if (candidate != first) {
      *second = candidate;
      return true;
    }
  }
  for (BufferId candidate : first_level) {
    if (candidate != first) {
      *second = candidate;
      return true;
    }
  }
  return false;
}

bool ProposeGraphGuidedSwap(const DsaResult& current, const NodeMap& nodes,
                            const std::vector<BufferId>& order, std::mt19937_64* random,
                            std::vector<BufferId>* candidate_order) {
  if (!current.solution) return false;
  const PositionMap positions = IndexPositions(order);
  std::vector<BufferId> high_water_nodes;
  for (BufferId id : order) {
    const auto node_it = nodes.find(id);
    if (node_it == nodes.end() || node_it->second->pinned) continue;
    const detail::PlacementSearchNode& node = *node_it->second;
    const Placement* placement = current.solution->Find(id);
    if (placement == nullptr) continue;
    const auto peak = current.objective.peak_by_pool.find(placement->pool);
    if (peak == current.objective.peak_by_pool.end()) continue;
    if (placement->offset <= std::numeric_limits<std::uint64_t>::max() - node.size &&
        placement->offset + node.size == peak->second) {
      high_water_nodes.push_back(id);
    }
  }
  if (high_water_nodes.empty()) return false;

  const BufferId boundary = high_water_nodes[RandomIndex(random, high_water_nodes.size())];
  const detail::PlacementSearchNode& boundary_node = *nodes.at(boundary);
  std::vector<BufferId> first_level;
  std::vector<BufferId> second_level;
  for (BufferId neighbor : boundary_node.conflicts) {
    const auto position = positions.find(neighbor);
    if (position == positions.end() || position->second >= positions.at(boundary) ||
        !IsMovable(nodes, neighbor)) {
      continue;
    }
    first_level.push_back(neighbor);
    const detail::PlacementSearchNode& first_node = *nodes.at(neighbor);
    for (BufferId second_neighbor : first_node.conflicts) {
      const auto second_position = positions.find(second_neighbor);
      if (second_position != positions.end() && second_position->second < position->second &&
          IsMovable(nodes, second_neighbor)) {
        // TVM intentionally keeps duplicates here; they weight frequently
        // reached second-level neighbors more heavily.
        second_level.push_back(second_neighbor);
      }
    }
  }
  if (first_level.empty()) return false;
  SortByPosition(&first_level, positions);
  SortByPosition(&second_level, positions);

  const BufferId first = first_level[RandomIndex(random, first_level.size())];
  BufferId second = first;
  if (!ChooseDistinctSecond(first, first_level, second_level, random, &second)) return false;

  *candidate_order = order;
  std::swap((*candidate_order)[positions.at(first)], (*candidate_order)[positions.at(second)]);
  return true;
}

bool AcceptWorsePeakMove(const DsaResult& current, const DsaResult& candidate, std::size_t attempt,
                         std::uint32_t scale_percent, std::mt19937_64* random) {
  if (scale_percent == 0 || candidate.objective.total_peak <= current.objective.total_peak) {
    return false;
  }
  const long double delta = static_cast<long double>(candidate.objective.total_peak) -
                            static_cast<long double>(current.objective.total_peak);
  const long double scale = static_cast<long double>(std::min<std::uint32_t>(100, scale_percent));
  const long double percent = scale * delta /
                              static_cast<long double>(candidate.objective.total_peak) /
                              static_cast<long double>(attempt + 1);
  return static_cast<long double>((*random)() % 100) < percent;
}

}  // namespace

TvmHillClimbSolver::TvmHillClimbSolver(TvmHillClimbOptions options) : options_(options) {}

const char* TvmHillClimbSolver::Name() const noexcept { return "tvm_hill_climb"; }

SolverCapabilities TvmHillClimbSolver::Capabilities() const noexcept {
  SolverCapabilities capabilities;
  capabilities.multi_interval = true;
  capabilities.reuse_cost = true;
  capabilities.colocations = true;
  capabilities.separations = true;
  capabilities.temporal_exclusions = true;
  capabilities.pinned_allocations = true;
  capabilities.reserved_ranges = true;
  capabilities.multi_pool = true;
  capabilities.lexicographic_objective = true;
  capabilities.capacity_objective = true;
  capabilities.peak_objective = true;
  return capabilities;
}

DsaResult TvmHillClimbSolver::Solve(const DsaProblem& problem) const {
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
      result.diagnostics.push_back("tvm_hill_climb does not support feature '" + feature + "'");
    }
    for (const std::string& objective : compatibility.unsupported_objectives) {
      result.diagnostics.push_back("tvm_hill_climb does not support objective '" + objective + "'");
    }
    return result;
  }

  const detail::PlacementSearchSpace search_space = detail::BuildPlacementSearchSpace(problem);
  if (!search_space.errors.empty()) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = search_space.errors;
    return result;
  }
  if (search_space.flexible_pools) {
    DsaResult result;
    result.status = SolveStatus::kUnsupported;
    result.diagnostics.push_back("tvm_hill_climb does not support flexible pool assignment");
    return result;
  }

  std::vector<BufferId> current_order = InitialOrder(search_space);
  DsaResult current = detail::PlaceWithOrder(problem, current_order);
  DsaResult best = current;
  Score current_score = ResultScore(problem, current);
  Score best_score = current_score;
  if (!current.solution || current_order.size() < 2 || options_.max_attempts == 0) return best;

  const NodeMap nodes = IndexNodes(search_space);
  std::mt19937_64 random(options_.seed);
  std::size_t evaluated = 1;
  std::size_t proposed = 0;
  std::size_t accepted_better = 0;
  std::size_t accepted_worse = 0;
  std::size_t stalled = 0;

  for (std::size_t attempt = 0; attempt < options_.max_attempts; ++attempt) {
    if (options_.target_total_peak && best.status == SolveStatus::kFeasible &&
        best.objective.total_peak <= *options_.target_total_peak) {
      break;
    }
    std::vector<BufferId> candidate_order;
    if (!ProposeGraphGuidedSwap(current, nodes, current_order, &random, &candidate_order)) {
      ++stalled;
      continue;
    }
    ++proposed;
    DsaResult candidate = detail::PlaceWithOrder(problem, candidate_order);
    ++evaluated;
    const Score candidate_score = ResultScore(problem, candidate);
    const bool better = candidate_score < current_score;
    const bool accept_worse =
        !better && AcceptWorsePeakMove(current, candidate, attempt,
                                       options_.worse_move_scale_percent, &random);
    if (!better && !accept_worse) continue;

    if (better) {
      ++accepted_better;
    } else {
      ++accepted_worse;
    }
    current = std::move(candidate);
    current_order = std::move(candidate_order);
    current_score = candidate_score;
    if (current_score < best_score) {
      best = current;
      best_score = current_score;
    }
  }

  best.diagnostics.push_back(
      "TVM-style hill climb evaluated " + std::to_string(evaluated) +
      " placement orders with seed " + std::to_string(options_.seed) + "; proposed " +
      std::to_string(proposed) + ", accepted " + std::to_string(accepted_better) + " improving/" +
      std::to_string(accepted_worse) + " worse, stalled " + std::to_string(stalled));
  return best;
}

}  // namespace dsa

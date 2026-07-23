// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/reuse_penalty_local_search_solver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
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

using NodePair = detail::ReuseNodePair;
using Score = std::vector<std::uint64_t>;

struct Candidate {
  std::vector<BufferId> order;
  std::set<NodePair> promoted;
  DsaResult result;
  Score score;
};

SolverCapabilities LocalSearchCapabilities() {
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

Score ResultScore(const DsaProblem& problem, const DsaResult& result) {
  if (!result.solution.has_value() ||
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

Candidate Evaluate(const DsaProblem& problem, const detail::ReusePenaltySearchSpace& search,
                   std::vector<BufferId> order, std::set<NodePair> promoted) {
  const DsaProblem promoted_problem = detail::WithPromotedReuseEdges(problem, search, promoted);
  DsaResult result = detail::PlaceWithOrder(promoted_problem, order);
  if (result.solution.has_value()) {
    result.objective = EvaluateObjective(problem, result.solution.value());
  }
  Score score = ResultScore(problem, result);
  return {std::move(order), std::move(promoted), std::move(result), std::move(score)};
}

std::set<NodePair> EverySoftPair(const detail::ReusePenaltySearchSpace& search) {
  std::set<NodePair> result;
  for (const auto& [pair, weight] : search.soft_weights) {
    static_cast<void>(weight);
    result.insert(pair);
  }
  return result;
}

std::vector<BufferId> NodeOrder(const DsaProblem& problem,
                                const detail::ReusePenaltySearchSpace& search) {
  std::vector<BufferId> result;
  for (std::size_t node : detail::DefaultReuseNodeOrder(problem, search)) {
    result.push_back(search.placement.nodes[node].representative);
  }
  return result;
}

std::set<NodePair> ActivatedPairs(const detail::ReusePenaltySearchSpace& search,
                                  const DsaSolution& solution) {
  std::set<NodePair> result;
  for (const auto& [pair, weight] : search.soft_weights) {
    static_cast<void>(weight);
    const detail::PlacementSearchNode& first = search.placement.nodes[pair.first];
    const detail::PlacementSearchNode& second = search.placement.nodes[pair.second];
    const Placement* first_placement = solution.Find(first.representative);
    const Placement* second_placement = solution.Find(second.representative);
    if (first_placement != nullptr && second_placement != nullptr &&
        first_placement->pool == second_placement->pool &&
        AddressRangesOverlap(first_placement->offset, first.size, second_placement->offset,
                             second.size)) {
      result.insert(pair);
    }
  }
  return result;
}

std::vector<NodePair> OrderedSoftMoves(const detail::ReusePenaltySearchSpace& search,
                                       const Candidate& current) {
  const std::set<NodePair> activated = current.result.solution.has_value()
                                           ? ActivatedPairs(search, current.result.solution.value())
                                           : std::set<NodePair>{};
  std::vector<NodePair> result;
  result.reserve(search.soft_weights.size());
  for (const auto& [pair, weight] : search.soft_weights) {
    static_cast<void>(weight);
    result.push_back(pair);
  }
  std::sort(result.begin(), result.end(), [&](const NodePair& first, const NodePair& second) {
    const bool first_promoted = current.promoted.count(first) != 0;
    const bool second_promoted = current.promoted.count(second) != 0;
    const bool first_active = activated.count(first) != 0;
    const bool second_active = activated.count(second) != 0;
    const int first_class = !first_promoted && first_active ? 0 : first_promoted ? 1 : 2;
    const int second_class = !second_promoted && second_active ? 0 : second_promoted ? 1 : 2;
    if (first_class != second_class) {
      return first_class < second_class;
    }
    const std::uint64_t first_weight = search.soft_weights.at(first);
    const std::uint64_t second_weight = search.soft_weights.at(second);
    if (first_class == 1 && first_weight != second_weight) {
      return first_weight < second_weight;
    }
    if (first_class != 1 && first_weight != second_weight) {
      return first_weight > second_weight;
    }
    return first < second;
  });
  return result;
}

void ApplyOrderMove(std::vector<BufferId>* order, std::mt19937_64* random) {
  if (order->size() < 2) {
    return;
  }
  std::uniform_int_distribution<std::size_t> index(0, order->size() - 1);
  std::size_t first = index(*random);
  std::size_t second = index(*random);
  while (first == second) {
    second = index(*random);
  }
  if (((*random)() & 1U) == 0) {
    std::swap((*order)[first], (*order)[second]);
    return;
  }
  const BufferId value = (*order)[first];
  order->erase(order->begin() + static_cast<std::ptrdiff_t>(first));
  if (first < second) {
    --second;
  }
  order->insert(order->begin() + static_cast<std::ptrdiff_t>(second), value);
}

bool Better(const Candidate& first, const Candidate& second) {
  if (first.score != second.score) {
    return first.score < second.score;
  }
  if (first.promoted.size() != second.promoted.size()) {
    return first.promoted.size() > second.promoted.size();
  }
  return first.order < second.order;
}

void Toggle(std::set<NodePair>* promoted, const NodePair& pair) {
  const auto found = promoted->find(pair);
  if (found == promoted->end()) {
    promoted->insert(pair);
  } else {
    promoted->erase(found);
  }
}

}  // namespace

ReusePenaltyLocalSearchSolver::ReusePenaltyLocalSearchSolver(ReusePenaltyLocalSearchOptions options)
    : options_(options) {}

const char* ReusePenaltyLocalSearchSolver::Name() const noexcept {
  return "reuse_penalty_local_search";
}

SolverCapabilities ReusePenaltyLocalSearchSolver::Capabilities() const noexcept {
  return LocalSearchCapabilities();
}

DsaResult ReusePenaltyLocalSearchSolver::Solve(const DsaProblem& problem) const {
  const std::vector<std::string> validation = ValidateProblem(problem);
  if (!validation.empty()) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = validation;
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
  for (const Pool& pool : problem.pools) {
    if (!pool.capacity.has_value()) {
      DsaResult result;
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.emplace_back("reuse_penalty_local_search requires fixed pool capacities");
      return result;
    }
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
    result.diagnostics.emplace_back("reuse_penalty_local_search does not support flexible pools");
    return result;
  }

  const std::vector<BufferId> seed_order = NodeOrder(problem, search);
  const std::set<NodePair> all_promoted = EverySoftPair(search);
  std::size_t evaluations = 0;
  Candidate best = Evaluate(problem, search, seed_order, {});
  ++evaluations;
  if (evaluations < options_.max_evaluations) {
    Candidate strict = Evaluate(problem, search, seed_order, all_promoted);
    ++evaluations;
    if (Better(strict, best)) {
      best = std::move(strict);
    }
  }
  if (options_.max_evaluations <= evaluations || options_.restarts == 0 ||
      (seed_order.size() < 2 && search.soft_weights.empty())) {
    best.result.solver_metrics = {
        {"evaluations", evaluations},
        {"accepted_moves", 0},
        {"promoted_edges", best.promoted.size()},
    };
    return best.result;
  }

  std::mt19937_64 random(options_.seed);
  std::uint64_t accepted_moves = 0;
  std::uint64_t soft_moves = 0;
  std::uint64_t order_moves = 0;
  for (std::size_t restart = 0;
       restart < options_.restarts && evaluations < options_.max_evaluations; ++restart) {
    Candidate current = best;
    if (restart != 0) {
      std::shuffle(current.order.begin(), current.order.end(), random);
      for (const auto& [pair, weight] : search.soft_weights) {
        static_cast<void>(weight);
        if ((random() & 1U) != 0) {
          Toggle(&current.promoted, pair);
        }
      }
      current = Evaluate(problem, search, std::move(current.order), std::move(current.promoted));
      ++evaluations;
      if (Better(current, best)) {
        best = current;
      }
    }

    std::size_t stagnation = 0;
    while (evaluations < options_.max_evaluations && stagnation < options_.stagnation_limit) {
      Candidate next = current;
      bool improved = false;

      const std::vector<NodePair> soft_candidates = OrderedSoftMoves(search, current);
      const std::size_t soft_limit =
          std::min(options_.soft_moves_per_iteration, soft_candidates.size());
      for (std::size_t index = 0; index < soft_limit && evaluations < options_.max_evaluations;
           ++index) {
        std::set<NodePair> promoted = current.promoted;
        Toggle(&promoted, soft_candidates[index]);
        Candidate candidate = Evaluate(problem, search, current.order, std::move(promoted));
        ++evaluations;
        ++soft_moves;
        if (Better(candidate, next)) {
          next = std::move(candidate);
          improved = true;
        }
      }

      for (std::size_t move = 0;
           move < options_.order_moves_per_iteration && evaluations < options_.max_evaluations;
           ++move) {
        std::vector<BufferId> order = current.order;
        ApplyOrderMove(&order, &random);
        Candidate candidate = Evaluate(problem, search, std::move(order), current.promoted);
        ++evaluations;
        ++order_moves;
        if (Better(candidate, next)) {
          next = std::move(candidate);
          improved = true;
        }
      }

      if (improved && Better(next, current)) {
        current = std::move(next);
        ++accepted_moves;
        stagnation = 0;
        if (Better(current, best)) {
          best = current;
        }
      } else {
        ++stagnation;
        if (stagnation == options_.stagnation_limit / 2 && evaluations < options_.max_evaluations) {
          ApplyOrderMove(&current.order, &random);
          if (!soft_candidates.empty()) {
            Toggle(&current.promoted, soft_candidates[random() % soft_candidates.size()]);
          }
          current =
              Evaluate(problem, search, std::move(current.order), std::move(current.promoted));
          ++evaluations;
          if (Better(current, best)) {
            best = current;
          }
        }
      }
    }
  }

  best.result.solver_metrics = {
      {"evaluations", evaluations},
      {"accepted_moves", accepted_moves},
      {"soft_moves", soft_moves},
      {"order_moves", order_moves},
      {"promoted_edges", best.promoted.size()},
      {"soft_edges", search.soft_weights.size()},
  };
  best.result.diagnostics.emplace_back(
      "reuse_penalty_local_search searched both the placement order and "
      "the promoted soft-edge set using full deterministic re-decodes");
  return best.result;
}

}  // namespace dsa

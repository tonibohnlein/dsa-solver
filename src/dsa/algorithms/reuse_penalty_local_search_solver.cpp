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
#include "dsa/algorithms/reuse_penalty_baseline_solvers.h"
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
    return Score(problem.objective.terms.size() + 2, std::numeric_limits<std::uint64_t>::max());
  }
  Score score;
  score.reserve(problem.objective.terms.size() + 2);
  // Capacity is a hard acceptance condition even for callers whose reporting
  // objective omits capacity_overflow. Keep infeasible states searchable for
  // repair, but rank every fitting placement ahead of every over-capacity one
  // and then prefer the smaller overflow among infeasible states.
  score.push_back(result.status == SolveStatus::kFeasible ? 0U : 1U);
  score.push_back(
      EvaluateObjectiveMetric(problem, result.objective, ObjectiveMetric::kCapacityOverflow));
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

std::optional<Candidate> StateSeedFromSolution(const DsaProblem& problem,
                                               const detail::ReusePenaltySearchSpace& search,
                                               const DsaSolution& solution) {
  std::vector<std::size_t> nodes(search.placement.nodes.size());
  for (std::size_t node = 0; node < nodes.size(); ++node) nodes[node] = node;
  std::sort(nodes.begin(), nodes.end(), [&](std::size_t first, std::size_t second) {
    const auto& first_node = search.placement.nodes[first];
    const auto& second_node = search.placement.nodes[second];
    const Placement* first_placement = solution.Find(first_node.representative);
    const Placement* second_placement = solution.Find(second_node.representative);
    if (first_placement == nullptr || second_placement == nullptr) {
      return first_node.representative < second_node.representative;
    }
    return std::tie(first_placement->pool, first_placement->offset, first_node.representative) <
           std::tie(second_placement->pool, second_placement->offset, second_node.representative);
  });
  std::vector<BufferId> order;
  order.reserve(nodes.size());
  for (std::size_t node : nodes) {
    order.push_back(search.placement.nodes[node].representative);
  }

  const std::set<NodePair> activated = ActivatedPairs(search, solution);
  std::set<NodePair> promoted;
  for (const auto& [pair, weight] : search.soft_weights) {
    static_cast<void>(weight);
    if (activated.count(pair) == 0) promoted.insert(pair);
  }
  return Evaluate(problem, search, std::move(order), std::move(promoted));
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

std::vector<BufferId> TargetedNodes(const detail::ReusePenaltySearchSpace& search,
                                    const Candidate& current) {
  std::set<std::size_t> targets;
  if (current.result.solution.has_value()) {
    for (const NodePair& pair : ActivatedPairs(search, current.result.solution.value())) {
      targets.insert(pair.first);
      targets.insert(pair.second);
    }
    for (const auto& [pool, peak] : current.result.objective.peak_by_pool) {
      for (std::size_t node = 0; node < search.placement.nodes.size(); ++node) {
        const auto& value = search.placement.nodes[node];
        const Placement* placement = current.result.solution->Find(value.representative);
        if (value.pool == pool && placement != nullptr &&
            !detail::AddOverflows(placement->offset, value.size) &&
            placement->offset + value.size == peak) {
          targets.insert(node);
        }
      }
    }
  }
  std::set<std::size_t> frontier = targets;
  for (std::size_t depth = 0; depth < 2; ++depth) {
    std::set<std::size_t> next;
    for (std::size_t node : frontier) {
      next.insert(search.hard_neighbors[node].begin(), search.hard_neighbors[node].end());
      for (const auto& [neighbor, weight] : search.soft_neighbors[node]) {
        static_cast<void>(weight);
        next.insert(neighbor);
      }
    }
    targets.insert(next.begin(), next.end());
    frontier = std::move(next);
  }
  std::vector<BufferId> result;
  for (std::size_t node : targets) {
    result.push_back(search.placement.nodes[node].representative);
  }
  return result;
}

void ApplyOrderMove(std::vector<BufferId>* order, const std::vector<BufferId>& targets,
                    std::mt19937_64* random) {
  if (order->size() < 2) {
    return;
  }
  std::uniform_int_distribution<std::size_t> index(0, order->size() - 1);
  std::size_t first = index(*random);
  if (!targets.empty()) {
    std::uniform_int_distribution<std::size_t> target_index(0, targets.size() - 1);
    const auto found = std::find(order->begin(), order->end(), targets[target_index(*random)]);
    if (found != order->end()) {
      first = static_cast<std::size_t>(std::distance(order->begin(), found));
    }
  }
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

bool AcceptWorse(const Candidate& candidate, const Candidate& current, std::size_t evaluation,
                 std::uint32_t scale_percent, std::mt19937_64* random) {
  if (scale_percent == 0 || candidate.score <= current.score ||
      candidate.score.size() != current.score.size()) {
    return false;
  }
  std::size_t differing = 0;
  while (differing < candidate.score.size() &&
         candidate.score[differing] == current.score[differing]) {
    ++differing;
  }
  if (differing == candidate.score.size() ||
      candidate.score[differing] < current.score[differing]) {
    return false;
  }
  const std::uint64_t delta = candidate.score[differing] - current.score[differing];
  const std::uint64_t base = std::max<std::uint64_t>(1, current.score[differing]);
  const long double probability = std::min<long double>(
      1.0L, static_cast<long double>(scale_percent) / 100.0L * static_cast<long double>(delta) /
                static_cast<long double>(base) / static_cast<long double>(evaluation + 1));
  std::uniform_real_distribution<long double> draw(0.0L, 1.0L);
  return draw(*random) < probability;
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
  std::optional<DsaResult> constructive_incumbent;
  Score constructive_score;
  std::vector<Candidate> constructive_state_seeds;
  const CanonicalGreedySolver canonical_seed;
  const PromoteRepairSolver repair_seed;
  for (const DsaSolver* solver : std::vector<const DsaSolver*>{&canonical_seed, &repair_seed}) {
    DsaResult candidate = solver->Solve(problem);
    if (!candidate.solution.has_value() || candidate.status != SolveStatus::kFeasible) {
      continue;
    }
    const Score score = ResultScore(problem, candidate);
    if (!constructive_incumbent.has_value() || score < constructive_score) {
      constructive_incumbent = candidate;
      constructive_score = score;
    }
    if (evaluations < options_.max_evaluations) {
      std::optional<Candidate> state = StateSeedFromSolution(problem, search, *candidate.solution);
      if (state.has_value()) {
        ++evaluations;
        if (Better(*state, best)) best = *state;
        constructive_state_seeds.push_back(std::move(*state));
      }
    }
  }
  if (evaluations < options_.max_evaluations) {
    Candidate strict = Evaluate(problem, search, seed_order, all_promoted);
    ++evaluations;
    if (Better(strict, best)) {
      best = std::move(strict);
    }
  }
  if (options_.max_evaluations <= evaluations || options_.restarts == 0 ||
      (seed_order.size() < 2 && search.soft_weights.empty())) {
    DsaResult output = best.result;
    if (constructive_incumbent.has_value() && constructive_score < best.score) {
      output = std::move(*constructive_incumbent);
    }
    output.solver_metrics = {
        {"evaluations", evaluations},
        {"accepted_moves", 0},
        {"promoted_edges", best.promoted.size()},
        {"constructive_seeds", 2},
        {"constructive_state_seeds", constructive_state_seeds.size()},
        {"output_from_constructive",
         constructive_incumbent.has_value() && constructive_score < best.score ? 1U : 0U},
    };
    return output;
  }

  std::mt19937_64 random(options_.seed);
  std::uint64_t accepted_moves = 0;
  std::uint64_t soft_moves = 0;
  std::uint64_t order_moves = 0;
  std::uint64_t accepted_worse_moves = 0;
  for (std::size_t restart = 0;
       restart < options_.restarts && evaluations < options_.max_evaluations; ++restart) {
    Candidate current =
        restart < constructive_state_seeds.size() ? constructive_state_seeds[restart] : best;
    if (restart >= constructive_state_seeds.size() && restart != 0) {
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
      std::optional<Candidate> perturbation;

      const std::vector<NodePair> soft_candidates = OrderedSoftMoves(search, current);
      const std::vector<BufferId> targeted_nodes = TargetedNodes(search, current);
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
        ApplyOrderMove(&order, targeted_nodes, &random);
        Candidate candidate = Evaluate(problem, search, std::move(order), current.promoted);
        ++evaluations;
        ++order_moves;
        if (Better(candidate, next)) {
          next = std::move(candidate);
          improved = true;
        } else if (!perturbation.has_value() || candidate.score < perturbation->score) {
          perturbation = std::move(candidate);
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
        if (perturbation.has_value() && AcceptWorse(*perturbation, current, evaluations,
                                                    options_.worse_move_scale_percent, &random)) {
          current = std::move(*perturbation);
          ++accepted_moves;
          ++accepted_worse_moves;
        }
        if (stagnation == options_.stagnation_limit / 2 && evaluations < options_.max_evaluations) {
          ApplyOrderMove(&current.order, targeted_nodes, &random);
          if (!soft_candidates.empty()) {
            std::uniform_int_distribution<std::size_t> edge(0, soft_candidates.size() - 1);
            Toggle(&current.promoted, soft_candidates[edge(random)]);
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

  DsaResult output = best.result;
  if (constructive_incumbent.has_value() && constructive_score < best.score) {
    output = std::move(*constructive_incumbent);
  }
  output.solver_metrics = {
      {"evaluations", evaluations},
      {"accepted_moves", accepted_moves},
      {"accepted_worse_moves", accepted_worse_moves},
      {"soft_moves", soft_moves},
      {"order_moves", order_moves},
      {"promoted_edges", best.promoted.size()},
      {"soft_edges", search.soft_weights.size()},
      {"constructive_seeds", 2},
      {"constructive_state_seeds", constructive_state_seeds.size()},
      {"output_from_constructive",
       constructive_incumbent.has_value() && constructive_score < best.score ? 1U : 0U},
  };
  output.diagnostics.emplace_back(
      "reuse_penalty_local_search searched both the placement order and "
      "the promoted soft-edge set using targeted neighborhoods and full "
      "deterministic re-decodes");
  return output;
}

}  // namespace dsa

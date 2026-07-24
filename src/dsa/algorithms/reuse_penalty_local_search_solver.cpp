// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/reuse_penalty_local_search_solver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
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
    return Score(problem.objective.terms.size() + 2,
                 std::numeric_limits<std::uint64_t>::max());
  }
  Score score;
  score.reserve(problem.objective.terms.size() + 2);
  // Capacity is a hard acceptance condition even when a reporting objective
  // omits capacity_overflow. Infeasible states remain searchable for repair,
  // but no lower reuse cost can make one outrank a fitting placement.
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

std::set<std::size_t> CombinedNeighbors(const detail::ReusePenaltySearchSpace& search,
                                        std::size_t node) {
  std::set<std::size_t> result = search.hard_neighbors[node];
  for (const auto& [pair, weight] : search.soft_weights) {
    static_cast<void>(weight);
    if (pair.first == node) {
      result.insert(pair.second);
    } else if (pair.second == node) {
      result.insert(pair.first);
    }
  }
  return result;
}

std::optional<std::size_t> TargetNode(const DsaProblem& problem,
                                      const detail::ReusePenaltySearchSpace& search,
                                      const Candidate& current, std::mt19937_64* random) {
  if (current.result.solution.has_value()) {
    if (EvaluateObjectiveMetric(problem, current.result.objective,
                                ObjectiveMetric::kCapacityOverflow) != 0) {
      std::optional<std::tuple<std::uint64_t, std::uint64_t, std::size_t>> highest;
      for (std::size_t node = 0; node < search.placement.nodes.size(); ++node) {
        const detail::PlacementSearchNode& value = search.placement.nodes[node];
        const Placement* placement = current.result.solution->Find(value.representative);
        const Pool* pool = problem.FindPool(value.pool);
        if (placement == nullptr || detail::AddOverflows(placement->offset, value.size)) {
          continue;
        }
        const std::uint64_t top = placement->offset + value.size;
        if (pool == nullptr || !pool->capacity.has_value() || top <= pool->capacity.value()) {
          continue;
        }
        const auto candidate = std::make_tuple(top - pool->capacity.value(), top, node);
        if (!highest.has_value() || candidate > highest.value()) {
          highest = candidate;
        }
      }
      if (highest.has_value()) {
        return std::get<2>(highest.value());
      }
    }

    const std::set<NodePair> activated = ActivatedPairs(search, current.result.solution.value());
    if (!activated.empty()) {
      std::vector<NodePair> pairs(activated.begin(), activated.end());
      std::vector<double> weights;
      weights.reserve(pairs.size());
      for (const NodePair& pair : pairs) {
        weights.push_back(static_cast<double>(search.soft_weights.at(pair)));
      }
      std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
      const NodePair pair = pairs[pick(*random)];
      return ((*random)() & 1U) == 0 ? pair.first : pair.second;
    }
  }
  if (search.placement.nodes.empty()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>((*random)() % search.placement.nodes.size());
}

bool ApplyTargetedOrderMove(const DsaProblem& problem,
                            const detail::ReusePenaltySearchSpace& search, const Candidate& current,
                            std::vector<BufferId>* order, std::mt19937_64* random) {
  const std::optional<std::size_t> target = TargetNode(problem, search, current, random);
  if (!target.has_value()) {
    return false;
  }
  std::set<std::size_t> neighborhood = CombinedNeighbors(search, target.value());
  const std::vector<std::size_t> first_level(neighborhood.begin(), neighborhood.end());
  for (std::size_t neighbor : first_level) {
    const std::set<std::size_t> second_level = CombinedNeighbors(search, neighbor);
    neighborhood.insert(second_level.begin(), second_level.end());
  }
  neighborhood.erase(target.value());
  if (neighborhood.empty()) {
    return false;
  }
  const std::size_t rank = static_cast<std::size_t>((*random)() % neighborhood.size());
  auto neighbor = neighborhood.begin();
  std::advance(neighbor, static_cast<std::ptrdiff_t>(rank));
  const BufferId first = search.placement.nodes[target.value()].representative;
  const BufferId second = search.placement.nodes[*neighbor].representative;
  const auto first_position = std::find(order->begin(), order->end(), first);
  const auto second_position = std::find(order->begin(), order->end(), second);
  if (first_position == order->end() || second_position == order->end()) {
    return false;
  }
  if (((*random)() & 1U) == 0) {
    std::iter_swap(first_position, second_position);
    return true;
  }
  const std::size_t first_index =
      static_cast<std::size_t>(std::distance(order->begin(), first_position));
  std::size_t second_index =
      static_cast<std::size_t>(std::distance(order->begin(), second_position));
  order->erase(order->begin() + static_cast<std::ptrdiff_t>(first_index));
  if (first_index < second_index) {
    --second_index;
  }
  order->insert(order->begin() + static_cast<std::ptrdiff_t>(second_index), first);
  return true;
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

bool AcceptNonImproving(const ReusePenaltyLocalSearchOptions& options, std::size_t evaluations,
                        std::mt19937_64* random) {
  if (options.initial_worse_acceptance_probability <= 0.0 || options.max_evaluations == 0) {
    return false;
  }
  const double progress = std::min(
      1.0, static_cast<double>(evaluations) / static_cast<double>(options.max_evaluations));
  const double probability =
      std::min(1.0, options.initial_worse_acceptance_probability) * (1.0 - progress);
  std::uniform_real_distribution<double> sample(0.0, 1.0);
  return sample(*random) < probability;
}

std::optional<Candidate> ConstructiveSeed(const DsaProblem& problem,
                                          const detail::ReusePenaltySearchSpace& search,
                                          const DsaResult& result, bool promote_separated_pairs) {
  if (result.status != SolveStatus::kFeasible || !result.solution.has_value()) {
    return std::nullopt;
  }
  std::vector<std::pair<std::pair<PoolId, std::uint64_t>, BufferId>> ranked;
  ranked.reserve(search.placement.nodes.size());
  for (const detail::PlacementSearchNode& node : search.placement.nodes) {
    const Placement* placement = result.solution->Find(node.representative);
    if (placement == nullptr) {
      return std::nullopt;
    }
    ranked.push_back({{placement->pool, placement->offset}, node.representative});
  }
  std::sort(ranked.begin(), ranked.end());
  std::vector<BufferId> order;
  order.reserve(ranked.size());
  for (const auto& [placement, representative] : ranked) {
    static_cast<void>(placement);
    order.push_back(representative);
  }

  std::set<NodePair> promoted;
  if (promote_separated_pairs) {
    const std::set<NodePair> activated = ActivatedPairs(search, result.solution.value());
    for (const auto& [pair, weight] : search.soft_weights) {
      static_cast<void>(weight);
      if (activated.count(pair) == 0) {
        promoted.insert(pair);
      }
    }
  }
  return Evaluate(problem, search, std::move(order), std::move(promoted));
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
  std::uint64_t constructive_seeds = 0;
  std::optional<DsaResult> constructive_incumbent;
  Score constructive_score;
  auto retain_constructive_incumbent = [&](const DsaResult& candidate) {
    if (candidate.status != SolveStatus::kFeasible || !candidate.solution.has_value()) {
      return;
    }
    const Score score = ResultScore(problem, candidate);
    if (!constructive_incumbent.has_value() || score < constructive_score) {
      constructive_incumbent = candidate;
      constructive_score = score;
    }
  };
  if (evaluations < options_.max_evaluations) {
    Candidate strict = Evaluate(problem, search, seed_order, all_promoted);
    ++evaluations;
    if (Better(strict, best)) {
      best = std::move(strict);
    }
  }
  if (evaluations < options_.max_evaluations) {
    DsaProblem seed_problem = problem;
    seed_problem.objective = FitThenMinimizeReuseCostObjective();
    const DsaResult canonical = CanonicalGreedySolver().Solve(seed_problem);
    retain_constructive_incumbent(canonical);
    if (std::optional<Candidate> seed = ConstructiveSeed(problem, search, canonical, false);
        seed.has_value()) {
      ++evaluations;
      ++constructive_seeds;
      if (Better(seed.value(), best)) {
        best = std::move(seed).value();
      }
    }
  }
  if (evaluations < options_.max_evaluations) {
    DsaProblem seed_problem = problem;
    seed_problem.objective = FitThenMinimizeReuseCostObjective();
    const DsaResult repaired = PromoteRepairSolver().Solve(seed_problem);
    retain_constructive_incumbent(repaired);
    if (std::optional<Candidate> seed = ConstructiveSeed(problem, search, repaired, true);
        seed.has_value()) {
      ++evaluations;
      ++constructive_seeds;
      if (Better(seed.value(), best)) {
        best = std::move(seed).value();
      }
    }
  }
  if (options_.max_evaluations <= evaluations || options_.restarts == 0 ||
      (seed_order.size() < 2 && search.soft_weights.empty())) {
    DsaResult output = best.result;
    const bool output_from_constructive =
        constructive_incumbent.has_value() && constructive_score < best.score;
    if (output_from_constructive) {
      output = constructive_incumbent.value();
    }
    output.solver_metrics = {
        {"evaluations", evaluations},
        {"accepted_moves", 0},
        {"promoted_edges", best.promoted.size()},
        {"constructive_seeds", constructive_seeds},
        {"output_from_constructive", output_from_constructive ? 1U : 0U},
    };
    return output;
  }

  std::mt19937_64 random(options_.seed);
  std::uint64_t accepted_moves = 0;
  std::uint64_t accepted_non_improving_moves = 0;
  std::uint64_t soft_moves = 0;
  std::uint64_t order_moves = 0;
  std::uint64_t targeted_order_moves = 0;
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
      std::optional<Candidate> accepted_escape;
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
        } else if (!Better(candidate, current) &&
                   AcceptNonImproving(options_, evaluations, &random) &&
                   (!accepted_escape.has_value() || Better(candidate, accepted_escape.value()))) {
          accepted_escape = std::move(candidate);
        }
      }

      for (std::size_t move = 0;
           move < options_.order_moves_per_iteration && evaluations < options_.max_evaluations;
           ++move) {
        std::vector<BufferId> order = current.order;
        if (ApplyTargetedOrderMove(problem, search, current, &order, &random)) {
          ++targeted_order_moves;
        } else {
          ApplyOrderMove(&order, &random);
        }
        Candidate candidate = Evaluate(problem, search, std::move(order), current.promoted);
        ++evaluations;
        ++order_moves;
        if (Better(candidate, next)) {
          next = std::move(candidate);
          improved = true;
        } else if (!Better(candidate, current) &&
                   AcceptNonImproving(options_, evaluations, &random) &&
                   (!accepted_escape.has_value() || Better(candidate, accepted_escape.value()))) {
          accepted_escape = std::move(candidate);
        }
      }

      if (improved && Better(next, current)) {
        current = std::move(next);
        ++accepted_moves;
        stagnation = 0;
        if (Better(current, best)) {
          best = current;
        }
      } else if (accepted_escape.has_value()) {
        current = std::move(accepted_escape).value();
        ++accepted_moves;
        ++accepted_non_improving_moves;
        ++stagnation;
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

  DsaResult output = best.result;
  const bool output_from_constructive =
      constructive_incumbent.has_value() && constructive_score < best.score;
  if (output_from_constructive) {
    output = constructive_incumbent.value();
  }
  output.solver_metrics = {
      {"evaluations", evaluations},
      {"accepted_moves", accepted_moves},
      {"accepted_non_improving_moves", accepted_non_improving_moves},
      {"soft_moves", soft_moves},
      {"order_moves", order_moves},
      {"targeted_order_moves", targeted_order_moves},
      {"promoted_edges", best.promoted.size()},
      {"soft_edges", search.soft_weights.size()},
      {"constructive_seeds", constructive_seeds},
      {"output_from_constructive", output_from_constructive ? 1U : 0U},
  };
  output.diagnostics.emplace_back(
      "reuse_penalty_local_search searched both the placement order and "
      "the promoted soft-edge set using targeted two-level neighborhoods, "
      "decaying non-improving acceptance, and full deterministic re-decodes");
  return output;
}

}  // namespace dsa

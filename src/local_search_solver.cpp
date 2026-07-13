#include "dsa/local_search_solver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "detail/placement_engine.h"
#include "dsa/validator.h"

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

void ApplyRandomMove(std::vector<BufferId>* order, std::mt19937_64* random) {
  if (order->size() < 2) return;
  std::uniform_int_distribution<std::size_t> index_distribution(0, order->size() - 1);
  std::size_t first = index_distribution(*random);
  std::size_t second = index_distribution(*random);
  while (second == first) second = index_distribution(*random);
  if (first > second) std::swap(first, second);

  switch ((*random)() % 3) {
    case 0:
      std::swap((*order)[first], (*order)[second]);
      break;
    case 1: {
      const BufferId moved = (*order)[second];
      order->erase(order->begin() + static_cast<std::ptrdiff_t>(second));
      order->insert(order->begin() + static_cast<std::ptrdiff_t>(first), moved);
      break;
    }
    default:
      std::reverse(order->begin() + static_cast<std::ptrdiff_t>(first),
                   order->begin() + static_cast<std::ptrdiff_t>(second + 1));
      break;
  }
}

}  // namespace

LocalSearchSolver::LocalSearchSolver(LocalSearchOptions options) : options_(options) {}

const char* LocalSearchSolver::Name() const noexcept { return "local_search"; }

SolverCapabilities LocalSearchSolver::Capabilities() const noexcept {
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

DsaResult LocalSearchSolver::Solve(const DsaProblem& problem) const {
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
      result.diagnostics.push_back("local_search does not support feature '" + feature + "'");
    }
    for (const std::string& objective : compatibility.unsupported_objectives) {
      result.diagnostics.push_back("local_search does not support objective '" + objective + "'");
    }
    return result;
  }
  std::vector<BufferId> seed_order = detail::DefaultPlacementOrder(problem);
  DsaResult best = detail::PlaceWithOrder(problem, seed_order);
  if (best.status == SolveStatus::kInvalidProblem || best.status == SolveStatus::kUnsupported ||
      best.status == SolveStatus::kInfeasibleProven || seed_order.size() < 2 ||
      options_.max_iterations == 0 || options_.restarts == 0) {
    return best;
  }

  std::vector<BufferId> best_order = seed_order;
  Score best_score = ResultScore(problem, best);
  std::mt19937_64 random(options_.seed);
  const std::size_t iterations_per_restart =
      std::max<std::size_t>(1, options_.max_iterations / options_.restarts);
  std::size_t evaluated = 1;

  for (std::size_t restart = 0; restart < options_.restarts; ++restart) {
    std::vector<BufferId> current_order = restart == 0 ? seed_order : best_order;
    if (restart != 0) {
      const std::size_t perturbations = std::max<std::size_t>(2, current_order.size() / 8);
      for (std::size_t i = 0; i < perturbations; ++i) ApplyRandomMove(&current_order, &random);
    }
    DsaResult current = detail::PlaceWithOrder(problem, current_order);
    Score current_score = ResultScore(problem, current);
    std::size_t stagnation = 0;

    for (std::size_t iteration = 0; iteration < iterations_per_restart; ++iteration) {
      std::vector<BufferId> candidate_order = current_order;
      ApplyRandomMove(&candidate_order, &random);
      DsaResult candidate = detail::PlaceWithOrder(problem, candidate_order);
      ++evaluated;
      const Score candidate_score = ResultScore(problem, candidate);
      if (candidate_score < current_score) {
        current = std::move(candidate);
        current_order = std::move(candidate_order);
        current_score = candidate_score;
        stagnation = 0;
        if (current_score < best_score) {
          best = current;
          best_order = current_order;
          best_score = current_score;
        }
      } else {
        ++stagnation;
      }

      if (options_.stagnation_limit != 0 && stagnation >= options_.stagnation_limit) {
        current_order = best_order;
        const std::size_t perturbations = std::max<std::size_t>(2, current_order.size() / 10);
        for (std::size_t i = 0; i < perturbations; ++i) ApplyRandomMove(&current_order, &random);
        current = detail::PlaceWithOrder(problem, current_order);
        current_score = ResultScore(problem, current);
        stagnation = 0;
      }
    }
  }

  best.diagnostics.push_back("local search evaluated " + std::to_string(evaluated) +
                             " placement orders with seed " + std::to_string(options_.seed));
  return best;
}

}  // namespace dsa

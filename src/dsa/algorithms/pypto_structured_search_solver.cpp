#include "dsa/algorithms/pypto_structured_search_solver.h"

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

bool MoveTo(std::vector<BufferId>* order, BufferId buffer, std::size_t destination) {
  const auto found = std::find(order->begin(), order->end(), buffer);
  if (found == order->end()) return false;
  const std::size_t source = static_cast<std::size_t>(std::distance(order->begin(), found));
  const BufferId value = *found;
  order->erase(found);
  if (source < destination) --destination;
  destination = std::min(destination, order->size());
  order->insert(order->begin() + static_cast<std::ptrdiff_t>(destination), value);
  return source != destination;
}

void ApplyGenericMove(std::vector<BufferId>* order, std::mt19937_64* random) {
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
    case 1:
      MoveTo(order, (*order)[second], first);
      break;
    default:
      std::reverse(order->begin() + static_cast<std::ptrdiff_t>(first),
                   order->begin() + static_cast<std::ptrdiff_t>(second + 1));
      break;
  }
}

std::vector<std::vector<BufferId>> PipelineBlocks(const DsaProblem& problem) {
  std::vector<std::vector<BufferId>> blocks;
  if (!problem.pypto_structure) return blocks;
  for (const PyptoPipelineGroup& group : problem.pypto_structure->pipeline_groups) {
    std::vector<PyptoPipelineMember> members = group.members;
    std::sort(members.begin(), members.end(), [](const auto& first, const auto& second) {
      return std::tie(first.stage, first.residue, first.buffer) <
             std::tie(second.stage, second.residue, second.buffer);
    });
    std::vector<BufferId> block;
    for (const PyptoPipelineMember& member : members) {
      if (std::find(block.begin(), block.end(), member.buffer) == block.end()) {
        block.push_back(member.buffer);
      }
    }
    if (block.size() > 1) blocks.push_back(std::move(block));
  }
  return blocks;
}

void ApplyPipelineMove(std::vector<BufferId>* order,
                       const std::vector<std::vector<BufferId>>& blocks, std::mt19937_64* random) {
  if (blocks.empty()) {
    ApplyGenericMove(order, random);
    return;
  }
  const std::vector<BufferId>& requested = blocks[(*random)() % blocks.size()];
  std::vector<BufferId> block;
  for (BufferId buffer : requested) {
    if (std::find(order->begin(), order->end(), buffer) != order->end()) block.push_back(buffer);
  }
  if (block.size() < 2) {
    ApplyGenericMove(order, random);
    return;
  }
  const std::set<BufferId> members(block.begin(), block.end());
  order->erase(std::remove_if(order->begin(), order->end(),
                              [&](BufferId buffer) { return members.count(buffer) != 0; }),
               order->end());
  std::uniform_int_distribution<std::size_t> position(0, order->size());
  order->insert(order->begin() + static_cast<std::ptrdiff_t>(position(*random)), block.begin(),
                block.end());
}

void ApplyPenaltyMove(std::vector<BufferId>* order, const DsaProblem& problem,
                      std::mt19937_64* random) {
  if (!problem.cost_model || problem.cost_model->reuse_penalties.empty()) {
    ApplyGenericMove(order, random);
    return;
  }
  const ReusePenalty& penalty =
      problem.cost_model->reuse_penalties[(*random)() % problem.cost_model->reuse_penalties.size()];
  const BufferId endpoint = ((*random)() & 1U) == 0 ? penalty.first : penalty.second;
  const std::size_t destination = ((*random)() & 1U) == 0 ? 0 : order->size();
  if (!MoveTo(order, endpoint, destination)) ApplyGenericMove(order, random);
}

std::vector<BufferId> AliasBuffers(const DsaProblem& problem) {
  std::vector<BufferId> aliases;
  if (problem.pypto_structure) {
    for (const PyptoAliasClass& alias_class : problem.pypto_structure->alias_classes) {
      if (alias_class.members.size() > 1) aliases.push_back(alias_class.buffer);
    }
  }
  return aliases;
}

void ApplyAliasMove(std::vector<BufferId>* order, const std::vector<BufferId>& aliases,
                    std::mt19937_64* random) {
  if (aliases.empty()) {
    ApplyGenericMove(order, random);
    return;
  }
  const BufferId buffer = aliases[(*random)() % aliases.size()];
  std::uniform_int_distribution<std::size_t> position(0, order->size());
  if (!MoveTo(order, buffer, position(*random))) ApplyGenericMove(order, random);
}

void ApplyStructuredMove(std::vector<BufferId>* order, const DsaProblem& problem,
                         const std::vector<std::vector<BufferId>>& pipeline_blocks,
                         const std::vector<BufferId>& aliases, std::mt19937_64* random) {
  switch ((*random)() % 6) {
    case 0:
    case 1:
    case 2:
      ApplyGenericMove(order, random);
      break;
    case 3:
      ApplyPipelineMove(order, pipeline_blocks, random);
      break;
    case 4:
      ApplyPenaltyMove(order, problem, random);
      break;
    default:
      ApplyAliasMove(order, aliases, random);
      break;
  }
}

}  // namespace

PyptoStructuredSearchSolver::PyptoStructuredSearchSolver(PyptoStructuredSearchOptions options)
    : options_(options) {}

const char* PyptoStructuredSearchSolver::Name() const noexcept { return "pypto_structured_search"; }

SolverCapabilities PyptoStructuredSearchSolver::Capabilities() const noexcept {
  SolverCapabilities capabilities;
  capabilities.multi_interval = true;
  capabilities.reuse_cost = true;
  capabilities.colocations = true;
  capabilities.separations = true;
  capabilities.temporal_exclusions = true;
  capabilities.pinned_allocations = true;
  capabilities.reserved_ranges = true;
  capabilities.multi_pool = true;
  capabilities.whole_slot_reuse = true;
  capabilities.lexicographic_objective = true;
  capabilities.capacity_objective = true;
  capabilities.peak_objective = true;
  return capabilities;
}

DsaResult PyptoStructuredSearchSolver::Solve(const DsaProblem& problem) const {
  const std::vector<std::string> validation = ValidateProblem(problem);
  if (!validation.empty()) {
    DsaResult result;
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = validation;
    return result;
  }
  if (!problem.pypto_structure) {
    DsaResult result;
    result.status = SolveStatus::kUnsupported;
    result.diagnostics.push_back("pypto_structured_search requires PyPTO structure");
    return result;
  }
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    DsaResult result;
    result.status = SolveStatus::kUnsupported;
    for (const std::string& feature : compatibility.unsupported_features) {
      result.diagnostics.push_back("pypto_structured_search does not support feature '" + feature +
                                   "'");
    }
    for (const std::string& objective : compatibility.unsupported_objectives) {
      result.diagnostics.push_back("pypto_structured_search does not support objective '" +
                                   objective + "'");
    }
    return result;
  }

  const std::vector<std::vector<BufferId>> pipeline_blocks = PipelineBlocks(problem);
  const std::vector<BufferId> aliases = AliasBuffers(problem);
  std::vector<BufferId> seed_order = detail::DefaultPlacementOrder(problem);
  DsaResult best = detail::PlaceWithOrder(problem, seed_order);
  if (best.status == SolveStatus::kInvalidProblem || best.status == SolveStatus::kUnsupported ||
      best.status == SolveStatus::kInfeasibleProven) {
    return best;
  }
  std::vector<BufferId> best_order = seed_order;
  Score best_score = ResultScore(problem, best);
  std::size_t evaluated = 1;
  if (seed_order.size() < 2 || options_.max_iterations <= 1 || options_.restarts == 0) {
    best.diagnostics.push_back("PyPTO structured search evaluated 1 placement order");
    return best;
  }

  std::mt19937_64 random(options_.seed);
  auto update_best = [&](const DsaResult& result, const std::vector<BufferId>& order,
                         const Score& score) {
    if (score < best_score) {
      best = result;
      best_order = order;
      best_score = score;
    }
  };

  for (std::size_t restart = 0; restart < options_.restarts && evaluated < options_.max_iterations;
       ++restart) {
    const std::size_t remaining_restarts = options_.restarts - restart;
    const std::size_t remaining_budget = options_.max_iterations - evaluated;
    const std::size_t restart_budget = remaining_budget / remaining_restarts +
                                       (remaining_budget % remaining_restarts == 0 ? 0 : 1);
    const std::size_t restart_end = evaluated + restart_budget;
    std::vector<BufferId> current_order = restart == 0 ? seed_order : best_order;
    DsaResult current = best;
    Score current_score = best_score;
    if (restart != 0) {
      const std::size_t perturbations = std::max<std::size_t>(2, current_order.size() / 8);
      for (std::size_t index = 0; index < perturbations; ++index) {
        ApplyStructuredMove(&current_order, problem, pipeline_blocks, aliases, &random);
      }
      current = detail::PlaceWithOrder(problem, current_order);
      ++evaluated;
      current_score = ResultScore(problem, current);
      update_best(current, current_order, current_score);
    }
    std::size_t stagnation = 0;
    while (evaluated < restart_end) {
      std::vector<BufferId> candidate_order = current_order;
      ApplyStructuredMove(&candidate_order, problem, pipeline_blocks, aliases, &random);
      DsaResult candidate = detail::PlaceWithOrder(problem, candidate_order);
      ++evaluated;
      const Score candidate_score = ResultScore(problem, candidate);
      if (candidate_score < current_score) {
        current = std::move(candidate);
        current_order = std::move(candidate_order);
        current_score = candidate_score;
        stagnation = 0;
        update_best(current, current_order, current_score);
      } else {
        ++stagnation;
      }
      if (options_.stagnation_limit != 0 && stagnation >= options_.stagnation_limit &&
          evaluated < restart_end) {
        current_order = best_order;
        const std::size_t perturbations = std::max<std::size_t>(2, current_order.size() / 10);
        for (std::size_t index = 0; index < perturbations; ++index) {
          ApplyStructuredMove(&current_order, problem, pipeline_blocks, aliases, &random);
        }
        current = detail::PlaceWithOrder(problem, current_order);
        ++evaluated;
        current_score = ResultScore(problem, current);
        update_best(current, current_order, current_score);
        stagnation = 0;
      }
    }
  }

  best.diagnostics.push_back("PyPTO structured search evaluated " + std::to_string(evaluated) +
                             " placement orders with " + std::to_string(pipeline_blocks.size()) +
                             " pipeline block(s) and " + std::to_string(aliases.size()) +
                             " multi-member alias identity/identities, seed " +
                             std::to_string(options_.seed));
  return best;
}

}  // namespace dsa

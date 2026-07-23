#include "dsa/algorithms/first_fit_solver.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "dsa/algorithms/placement_engine.h"
#include "dsa/algorithms/reuse_penalty_search.h"
#include "dsa/model/validator.h"

namespace dsa {
namespace {

using NodeOrder = std::vector<BufferId>;

std::int64_t EarliestBirth(const DsaProblem& problem, const detail::PlacementSearchNode& node) {
  std::int64_t birth = std::numeric_limits<std::int64_t>::max();
  for (BufferId member : node.members) {
    const Buffer* buffer = problem.FindBuffer(member);
    if (buffer == nullptr) continue;
    for (const Interval& interval : buffer->live_intervals) {
      birth = std::min(birth, interval.lower);
    }
  }
  return birth;
}

std::vector<std::uint64_t> ResultScore(const DsaProblem& problem, const DsaResult& result) {
  std::vector<std::uint64_t> score;
  score.reserve(problem.objective.terms.size() + 1);
  score.push_back(result.status == SolveStatus::kFeasible ? 0U : 1U);
  for (ObjectiveMetric metric : problem.objective.terms) {
    score.push_back(EvaluateObjectiveMetric(problem, result.objective, metric));
  }
  return score;
}

}  // namespace

const char* FirstFitSolver::Name() const noexcept { return "first_fit"; }

SolverCapabilities FirstFitSolver::Capabilities() const noexcept {
  SolverCapabilities capabilities;
  capabilities.multi_interval = true;
  capabilities.colocations = true;
  capabilities.separations = true;
  capabilities.temporal_exclusions = true;
  capabilities.pinned_allocations = true;
  capabilities.reserved_ranges = true;
  capabilities.multi_pool = true;
  capabilities.reuse_cost = true;
  capabilities.lexicographic_objective = true;
  capabilities.capacity_objective = true;
  capabilities.peak_objective = true;
  return capabilities;
}

DsaResult FirstFitSolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.StructurallyCompatible()) {
    DsaResult result;
    const std::vector<std::string> validation = ValidateProblem(problem);
    if (!validation.empty()) {
      result.status = SolveStatus::kInvalidProblem;
      result.diagnostics = validation;
      return result;
    }
    result.status = SolveStatus::kUnsupported;
    for (const std::string& feature : compatibility.unsupported_features) {
      result.diagnostics.push_back("first_fit does not support feature '" + feature + "'");
    }
    return result;
  }
  if (!problem.cost_model.has_value() || problem.cost_model->reuse_penalties.empty()) {
    DsaResult result = detail::PlaceWithOrder(problem, {});
    for (const std::string& objective : compatibility.unsupported_objectives) {
      result.diagnostics.push_back(
          "first_fit provides a structural baseline but does not optimize '" + objective + "'");
    }
    return result;
  }

  const detail::ReusePenaltySearchSpace search = detail::BuildReusePenaltySearchSpace(problem);
  if (!search.placement.errors.empty() || search.placement.flexible_pools) {
    DsaResult result = detail::PlaceWithOrder(problem, {});
    for (const std::string& objective : compatibility.unsupported_objectives) {
      result.diagnostics.push_back(
          "first_fit provides a structural baseline but does not optimize '" + objective + "'");
    }
    return result;
  }

  std::vector<NodeOrder> orders;
  orders.push_back(detail::DefaultPlacementOrder(problem));

  std::vector<std::size_t> nodes(search.placement.nodes.size());
  for (std::size_t index = 0; index < nodes.size(); ++index) nodes[index] = index;
  std::sort(nodes.begin(), nodes.end(), [&](std::size_t first, std::size_t second) {
    const auto first_key = std::make_tuple(EarliestBirth(problem, search.placement.nodes[first]),
                                           search.placement.nodes[first].representative);
    const auto second_key = std::make_tuple(EarliestBirth(problem, search.placement.nodes[second]),
                                            search.placement.nodes[second].representative);
    return first_key < second_key;
  });
  NodeOrder birth_order;
  for (std::size_t node : nodes) {
    birth_order.push_back(search.placement.nodes[node].representative);
  }
  orders.push_back(std::move(birth_order));

  std::vector<std::uint64_t> incident_weight(nodes.size(), 0);
  for (const auto& [pair, weight] : search.soft_weights) {
    incident_weight[pair.first] = detail::SaturatingAdd(incident_weight[pair.first], weight);
    incident_weight[pair.second] = detail::SaturatingAdd(incident_weight[pair.second], weight);
  }
  std::sort(nodes.begin(), nodes.end(), [&](std::size_t first, std::size_t second) {
    return std::make_tuple(
               std::numeric_limits<std::uint64_t>::max() - incident_weight[first],
               std::numeric_limits<std::uint64_t>::max() - search.placement.nodes[first].size,
               search.placement.nodes[first].representative) <
           std::make_tuple(
               std::numeric_limits<std::uint64_t>::max() - incident_weight[second],
               std::numeric_limits<std::uint64_t>::max() - search.placement.nodes[second].size,
               search.placement.nodes[second].representative);
  });
  NodeOrder soft_weight_order;
  for (std::size_t node : nodes) {
    soft_weight_order.push_back(search.placement.nodes[node].representative);
  }
  orders.push_back(std::move(soft_weight_order));

  std::vector<NodeOrder> unique_orders;
  for (NodeOrder& order : orders) {
    if (std::find(unique_orders.begin(), unique_orders.end(), order) == unique_orders.end()) {
      unique_orders.push_back(std::move(order));
    }
  }

  DsaResult result;
  std::vector<std::uint64_t> best_score;
  std::size_t selected = 0;
  for (std::size_t index = 0; index < unique_orders.size(); ++index) {
    DsaResult candidate = detail::PlaceWithOrder(problem, unique_orders[index]);
    const std::vector<std::uint64_t> score = ResultScore(problem, candidate);
    if (index == 0 || score < best_score) {
      result = std::move(candidate);
      best_score = score;
      selected = index;
    }
  }
  result.solver_metrics["first_fit_orders_evaluated"] = unique_orders.size();
  result.solver_metrics["first_fit_selected_order"] = selected;
  result.diagnostics.emplace_back(
      "first_fit generated penalty-blind placements in size, birth, and "
      "soft-incident-weight order and selected by the declared objective");
  for (const std::string& objective : compatibility.unsupported_objectives) {
    result.diagnostics.push_back(
        "first_fit provides a structural baseline but does not optimize '" + objective + "'");
  }
  return result;
}

}  // namespace dsa

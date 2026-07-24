// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/reuse_penalty_exact_solvers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "dsa/algorithms/canonical_exact_search.h"
#include "dsa/algorithms/reuse_penalty_baseline_solvers.h"
#include "dsa/algorithms/reuse_penalty_search.h"
#include "dsa/algorithms/solver.h"
#include "dsa/model/model.h"
#include "dsa/model/validator.h"

namespace dsa {
namespace {

using EdgeSet = std::set<std::size_t>;
using ObjectiveScore = std::vector<std::uint64_t>;

struct IndexedSoftEdge {
  detail::ReuseNodePair pair;
  std::uint64_t weight = 0;
};

ObjectiveScore ScoreResult(const DsaProblem& problem, const DsaResult& result) {
  ObjectiveScore score;
  score.reserve(problem.objective.terms.size());
  for (ObjectiveMetric metric : problem.objective.terms) {
    score.push_back(EvaluateObjectiveMetric(problem, result.objective, metric));
  }
  return score;
}

SolverCapabilities ExactCapabilities() {
  SolverCapabilities capabilities;
  capabilities.multi_interval = true;
  capabilities.reuse_cost = true;
  capabilities.colocations = true;
  capabilities.separations = true;
  capabilities.temporal_exclusions = true;
  capabilities.reserved_ranges = true;
  capabilities.multi_pool = true;
  capabilities.lexicographic_objective = true;
  capabilities.capacity_objective = true;
  capabilities.peak_objective = true;
  return capabilities;
}

DsaResult RejectExactProblem(const DsaProblem& problem, const char* solver_name,
                             const SolverCapabilities& capabilities) {
  DsaResult result;
  const std::vector<std::string> validation = ValidateProblem(problem);
  if (!validation.empty()) {
    result.status = SolveStatus::kInvalidProblem;
    result.diagnostics = validation;
    return result;
  }
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, capabilities);
  result.status = SolveStatus::kUnsupported;
  for (const std::string& feature : compatibility.unsupported_features) {
    result.diagnostics.emplace_back(std::string(solver_name) + " does not support feature '" +
                                    feature + "'");
  }
  for (const std::string& objective : compatibility.unsupported_objectives) {
    result.diagnostics.emplace_back(std::string(solver_name) + " does not support objective '" +
                                    objective + "'");
  }
  return result;
}

std::optional<std::string> PoolDecomposedReuseLimitation(const DsaProblem& problem) {
  for (const Pool& pool : problem.pools) {
    if (!pool.capacity.has_value()) {
      return "exact DSA-RP solvers require fixed pool capacities";
    }
  }
  std::optional<std::size_t> reuse_index;
  std::optional<std::size_t> first_peak_index;
  for (std::size_t index = 0; index < problem.objective.terms.size(); ++index) {
    const ObjectiveMetric metric = problem.objective.terms[index];
    if (metric == ObjectiveMetric::kReuseCost) {
      reuse_index = index;
    } else if ((metric == ObjectiveMetric::kTotalPeak || metric == ObjectiveMetric::kMaxPeak) &&
               !first_peak_index.has_value()) {
      first_peak_index = index;
    }
  }
  if (!reuse_index.has_value()) {
    return "implicit_hitting_set requires reuse_cost in the objective";
  }
  if (reuse_index.has_value() && first_peak_index.has_value() &&
      first_peak_index.value() < reuse_index.value()) {
    return "exact DSA-RP pool decomposition requires reuse_cost before peak "
           "metrics";
  }
  return std::nullopt;
}

std::vector<IndexedSoftEdge> IndexSoftEdges(const detail::ReusePenaltySearchSpace& search) {
  std::vector<IndexedSoftEdge> edges;
  edges.reserve(search.soft_weights.size());
  for (const auto& [pair, weight] : search.soft_weights) {
    edges.push_back({pair, weight});
  }
  return edges;
}

std::set<detail::ReuseNodePair> PromotedPairs(const std::vector<IndexedSoftEdge>& edges,
                                              const EdgeSet& promoted) {
  std::set<detail::ReuseNodePair> pairs;
  for (std::size_t edge : promoted) {
    pairs.insert(edges[edge].pair);
  }
  return pairs;
}

bool IsSubset(const EdgeSet& subset, const EdgeSet& superset) {
  return std::includes(superset.begin(), superset.end(), subset.begin(), subset.end());
}

struct HittingSetResult {
  EdgeSet selected;
  std::uint64_t cost = 0;
  std::uint64_t search_nodes = 0;
};

class ExactHittingSetMaster {
 public:
  ExactHittingSetMaster(const std::vector<IndexedSoftEdge>& edges,
                        const std::vector<EdgeSet>& cores)
      : edges_(edges), cores_(cores), selected_(edges.size(), false) {
    best_cost_ = std::numeric_limits<std::uint64_t>::max();
  }

  HittingSetResult Solve() {
    Search(0);
    return {best_selected_, best_cost_, search_nodes_};
  }

 private:
  void Search(std::uint64_t cost) {
    ++search_nodes_;
    if (cost > best_cost_) {
      return;
    }
    const EdgeSet* unhit = nullptr;
    for (const EdgeSet& core : cores_) {
      const bool hit =
          std::any_of(core.begin(), core.end(), [&](std::size_t edge) { return selected_[edge]; });
      if (!hit && (unhit == nullptr || core.size() < unhit->size())) {
        unhit = &core;
      }
    }
    if (unhit == nullptr) {
      EdgeSet candidate;
      for (std::size_t edge = 0; edge < selected_.size(); ++edge) {
        if (selected_[edge]) {
          candidate.insert(edge);
        }
      }
      if (cost < best_cost_ || (cost == best_cost_ && candidate < best_selected_)) {
        best_cost_ = cost;
        best_selected_ = std::move(candidate);
      }
      return;
    }
    std::vector<std::size_t> choices(unhit->begin(), unhit->end());
    std::sort(choices.begin(), choices.end(), [&](std::size_t first, std::size_t second) {
      return std::tie(edges_[first].weight, first) < std::tie(edges_[second].weight, second);
    });
    for (std::size_t edge : choices) {
      selected_[edge] = true;
      Search(detail::SaturatingAdd(cost, edges_[edge].weight));
      selected_[edge] = false;
    }
  }

  const std::vector<IndexedSoftEdge>& edges_;
  const std::vector<EdgeSet>& cores_;
  std::vector<bool> selected_;
  EdgeSet best_selected_;
  std::uint64_t best_cost_ = 0;
  std::uint64_t search_nodes_ = 0;
};

struct OracleResult {
  SolveStatus status = SolveStatus::kUnsupported;
  std::optional<DsaSolution> solution;
};

struct FeasibleCacheEntry {
  EdgeSet promoted;
  DsaSolution solution;
};

class PromotedDsaOracle {
 public:
  PromotedDsaOracle(const DsaProblem& problem, const detail::ReusePenaltySearchSpace& base_search,
                    const std::vector<IndexedSoftEdge>& edges, std::uint64_t node_limit)
      : problem_(problem), base_search_(base_search), edges_(edges), node_limit_(node_limit) {}

  OracleResult Solve(const EdgeSet& promoted) {
    ++calls_;
    for (const FeasibleCacheEntry& entry : feasible_cache_) {
      if (IsSubset(promoted, entry.promoted)) {
        ++cache_hits_;
        return {SolveStatus::kFeasible, entry.solution};
      }
    }
    for (const EdgeSet& core : infeasible_cores_) {
      if (IsSubset(core, promoted)) {
        ++cache_hits_;
        return {SolveStatus::kInfeasibleProven, std::nullopt};
      }
    }

    const DsaProblem promoted_problem =
        detail::WithPromotedReuseEdges(problem_, base_search_, PromotedPairs(edges_, promoted));
    const detail::ReusePenaltySearchSpace promoted_search =
        detail::BuildReusePenaltySearchSpace(promoted_problem);
    detail::CanonicalExactSearchOptions search_options;
    search_options.node_limit = node_limit_;
    search_options.stop_after_first = true;
    const detail::CanonicalExactSearchResult search_result =
        detail::RunCanonicalExactSearch(promoted_problem, promoted_search, search_options);
    search_nodes_ = detail::SaturatingAdd(search_nodes_, search_result.search_nodes);
    if (search_result.status == SolveStatus::kFeasible && search_result.offsets.has_value()) {
      DsaResult result = detail::BuildValidatedReuseResult(
          promoted_problem, promoted_search, search_result.offsets.value(), SolveStatus::kFeasible);
      if (result.status != SolveStatus::kFeasible || !result.solution.has_value()) {
        return {result.status, std::nullopt};
      }
      feasible_cache_.push_back({promoted, result.solution.value()});
      return {SolveStatus::kFeasible, result.solution};
    }
    return {search_result.status, std::nullopt};
  }

  void AddInfeasibleCore(const EdgeSet& core) { infeasible_cores_.push_back(core); }

  [[nodiscard]] std::uint64_t calls() const noexcept { return calls_; }
  [[nodiscard]] std::uint64_t cache_hits() const noexcept { return cache_hits_; }
  [[nodiscard]] std::uint64_t search_nodes() const noexcept { return search_nodes_; }

 private:
  const DsaProblem& problem_;
  const detail::ReusePenaltySearchSpace& base_search_;
  const std::vector<IndexedSoftEdge>& edges_;
  std::uint64_t node_limit_;
  std::vector<FeasibleCacheEntry> feasible_cache_;
  std::vector<EdgeSet> infeasible_cores_;
  std::uint64_t calls_ = 0;
  std::uint64_t cache_hits_ = 0;
  std::uint64_t search_nodes_ = 0;
};

EdgeSet Complement(const EdgeSet& selected, std::size_t edge_count) {
  EdgeSet complement;
  for (std::size_t edge = 0; edge < edge_count; ++edge) {
    if (selected.count(edge) == 0) {
      complement.insert(edge);
    }
  }
  return complement;
}

}  // namespace

CanonicalBranchAndBoundSolver::CanonicalBranchAndBoundSolver(CanonicalBranchAndBoundOptions options)
    : options_(options) {}

const char* CanonicalBranchAndBoundSolver::Name() const noexcept {
  return "canonical_branch_and_bound";
}

SolverCapabilities CanonicalBranchAndBoundSolver::Capabilities() const noexcept {
  return ExactCapabilities();
}

DsaResult CanonicalBranchAndBoundSolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    return RejectExactProblem(problem, Name(), Capabilities());
  }
  for (const Pool& pool : problem.pools) {
    if (!pool.capacity.has_value()) {
      DsaResult result;
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.emplace_back("canonical_branch_and_bound requires fixed pool capacities");
      return result;
    }
  }
  {
    std::optional<std::size_t> reuse_index;
    std::optional<std::size_t> first_peak_index;
    for (std::size_t index = 0; index < problem.objective.terms.size(); ++index) {
      const ObjectiveMetric metric = problem.objective.terms[index];
      if (metric == ObjectiveMetric::kReuseCost) {
        reuse_index = index;
      } else if ((metric == ObjectiveMetric::kTotalPeak || metric == ObjectiveMetric::kMaxPeak) &&
                 !first_peak_index.has_value()) {
        first_peak_index = index;
      }
    }
    if (reuse_index.has_value() && first_peak_index.has_value() &&
        first_peak_index.value() < reuse_index.value()) {
      DsaResult result;
      result.status = SolveStatus::kUnsupported;
      result.diagnostics.emplace_back(
          "canonical_branch_and_bound pool decomposition requires "
          "reuse_cost before peak metrics");
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

  std::optional<DsaResult> heuristic_incumbent;
  if (options_.max_search_nodes != 0) {
    const CanonicalGreedySolver canonical_greedy;
    const PromoteRepairSolver promote_repair;
    for (const DsaSolver* solver :
         std::vector<const DsaSolver*>{&canonical_greedy, &promote_repair}) {
      DsaResult candidate = solver->Solve(problem);
      if (candidate.status != SolveStatus::kFeasible || !candidate.solution.has_value()) {
        continue;
      }
      if (!heuristic_incumbent.has_value() ||
          ScoreResult(problem, candidate) < ScoreResult(problem, heuristic_incumbent.value())) {
        heuristic_incumbent = std::move(candidate);
      }
    }
  }
  detail::CanonicalExactSearchOptions search_options;
  search_options.node_limit = options_.max_search_nodes;
  search_options.stop_after_first = false;
  const detail::CanonicalExactSearchResult exact =
      detail::RunCanonicalExactSearch(problem, search, search_options);
  if (exact.offsets.has_value()) {
    DsaResult result =
        detail::BuildValidatedReuseResult(problem, search, exact.offsets.value(), exact.status);
    result.solver_metrics = {
        {"search_nodes", exact.search_nodes},
        {"candidate_branches", exact.candidate_branches},
        {"bound_prunes", exact.bound_prunes},
        {"optimality_proven", exact.status == SolveStatus::kFeasible ? 1U : 0U},
    };
    result.diagnostics.emplace_back(
        exact.status == SolveStatus::kFeasible
            ? "canonical branch-and-bound exhausted the canonical support "
              "search and proved the lexicographic optimum"
            : "canonical branch-and-bound reached its node limit with an "
              "incumbent");
    return result;
  }
  if (exact.status == SolveStatus::kTimeout && heuristic_incumbent.has_value()) {
    DsaResult result = std::move(heuristic_incumbent).value();
    result.status = SolveStatus::kTimeout;
    result.solver_metrics = {
        {"search_nodes", exact.search_nodes}, {"candidate_branches", exact.candidate_branches},
        {"bound_prunes", exact.bound_prunes}, {"optimality_proven", 0},
        {"heuristic_incumbent", 1},
    };
    result.diagnostics.emplace_back(
        "canonical branch-and-bound reached its node limit and retained the "
        "best canonical-greedy/promote-repair incumbent");
    return result;
  }
  DsaResult result;
  result.status = exact.status;
  result.solver_metrics = {
      {"search_nodes", exact.search_nodes},
      {"candidate_branches", exact.candidate_branches},
      {"bound_prunes", exact.bound_prunes},
      {"optimality_proven", exact.status == SolveStatus::kInfeasibleProven ? 1U : 0U},
  };
  result.diagnostics.emplace_back(
      exact.status == SolveStatus::kInfeasibleProven
          ? "canonical branch-and-bound exhausted the canonical support "
            "search and proved infeasibility"
          : "canonical branch-and-bound reached its node limit before finding "
            "a placement");
  return result;
}

ImplicitHittingSetSolver::ImplicitHittingSetSolver(ImplicitHittingSetOptions options)
    : options_(options) {}

const char* ImplicitHittingSetSolver::Name() const noexcept { return "implicit_hitting_set"; }

SolverCapabilities ImplicitHittingSetSolver::Capabilities() const noexcept {
  return ExactCapabilities();
}

DsaResult ImplicitHittingSetSolver::Solve(const DsaProblem& problem) const {
  const SolverCompatibility compatibility = CheckSolverCompatibility(problem, Capabilities());
  if (!compatibility.Compatible()) {
    return RejectExactProblem(problem, Name(), Capabilities());
  }
  if (const std::optional<std::string> limitation = PoolDecomposedReuseLimitation(problem);
      limitation.has_value()) {
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
  const std::vector<IndexedSoftEdge> edges = IndexSoftEdges(search);
  PromotedDsaOracle oracle(problem, search, edges, options_.max_oracle_nodes);
  std::vector<EdgeSet> cores;
  std::uint64_t master_nodes = 0;
  std::uint64_t shrink_calls = 0;

  for (std::size_t iteration = 0; iteration < options_.max_iterations; ++iteration) {
    const HittingSetResult master = ExactHittingSetMaster(edges, cores).Solve();
    master_nodes = detail::SaturatingAdd(master_nodes, master.search_nodes);
    const EdgeSet promoted = Complement(master.selected, edges.size());
    const OracleResult oracle_result = oracle.Solve(promoted);
    if (oracle_result.status == SolveStatus::kFeasible && oracle_result.solution.has_value()) {
      DsaResult result;
      result.status = SolveStatus::kFeasible;
      result.solution = oracle_result.solution;
      result.objective = EvaluateObjective(problem, result.solution.value());
      result.solver_metrics = {
          {"iterations", iteration + 1},
          {"verified_cores", cores.size()},
          {"master_search_nodes", master_nodes},
          {"oracle_calls", oracle.calls()},
          {"oracle_cache_hits", oracle.cache_hits()},
          {"oracle_search_nodes", oracle.search_nodes()},
          {"core_shrink_calls", shrink_calls},
          {"optimal_reuse_cost_proven", 1},
      };
      result.diagnostics.emplace_back(
          "implicit hitting set proved the minimum activated reuse weight; "
          "the returned oracle placement is not peak-tie-break optimized");
      return result;
    }
    if (oracle_result.status == SolveStatus::kTimeout) {
      DsaResult result;
      result.status = SolveStatus::kTimeout;
      result.solver_metrics = {
          {"iterations", iteration + 1},
          {"verified_cores", cores.size()},
          {"master_search_nodes", master_nodes},
          {"oracle_calls", oracle.calls()},
          {"oracle_cache_hits", oracle.cache_hits()},
          {"oracle_search_nodes", oracle.search_nodes()},
          {"core_shrink_calls", shrink_calls},
      };
      result.diagnostics.emplace_back(
          "implicit hitting set oracle reached its canonical-search node "
          "limit");
      return result;
    }
    if (oracle_result.status != SolveStatus::kInfeasibleProven) {
      DsaResult result;
      result.status = oracle_result.status;
      result.diagnostics.emplace_back(
          "implicit hitting set oracle could not classify the promoted "
          "subproblem");
      return result;
    }

    EdgeSet core = promoted;
    oracle.AddInfeasibleCore(core);
    if (options_.shrink_cores) {
      std::vector<std::size_t> candidates(core.begin(), core.end());
      for (std::size_t edge : candidates) {
        EdgeSet trial = core;
        trial.erase(edge);
        const OracleResult trial_result = oracle.Solve(trial);
        ++shrink_calls;
        if (trial_result.status == SolveStatus::kTimeout) {
          DsaResult result;
          result.status = SolveStatus::kTimeout;
          result.diagnostics.emplace_back(
              "implicit hitting set core shrinking reached the oracle node "
              "limit");
          return result;
        }
        if (trial_result.status == SolveStatus::kInfeasibleProven) {
          core = std::move(trial);
        }
      }
    }
    if (core.empty()) {
      DsaResult result;
      result.status = SolveStatus::kInfeasibleProven;
      result.solver_metrics = {
          {"iterations", iteration + 1},
          {"verified_cores", cores.size() + 1},
          {"master_search_nodes", master_nodes},
          {"oracle_calls", oracle.calls()},
          {"oracle_cache_hits", oracle.cache_hits()},
          {"oracle_search_nodes", oracle.search_nodes()},
          {"core_shrink_calls", shrink_calls},
          {"optimal_reuse_cost_proven", 1},
      };
      result.diagnostics.emplace_back(
          "the hard DSA problem is infeasible even after every soft edge is "
          "demoted");
      return result;
    }
    oracle.AddInfeasibleCore(core);
    if (std::find(cores.begin(), cores.end(), core) == cores.end()) {
      cores.push_back(std::move(core));
    }
  }

  DsaResult result;
  result.status = SolveStatus::kTimeout;
  result.solver_metrics = {
      {"iterations", options_.max_iterations},    {"verified_cores", cores.size()},
      {"master_search_nodes", master_nodes},      {"oracle_calls", oracle.calls()},
      {"oracle_cache_hits", oracle.cache_hits()}, {"oracle_search_nodes", oracle.search_nodes()},
      {"core_shrink_calls", shrink_calls},
  };
  result.diagnostics.emplace_back("implicit hitting set reached its iteration limit");
  return result;
}

}  // namespace dsa

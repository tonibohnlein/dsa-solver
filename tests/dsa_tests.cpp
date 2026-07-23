#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dsa/algorithms/cypress_relaxation_solver.h"
#include "dsa/algorithms/first_fit_solver.h"
#include "dsa/algorithms/local_search_solver.h"
#include "dsa/algorithms/pypto_structured_search_solver.h"
#include "dsa/algorithms/reuse_penalty_baseline_solvers.h"
#include "dsa/algorithms/reuse_penalty_exact_solvers.h"
#include "dsa/algorithms/reuse_penalty_local_search_solver.h"
#include "dsa/algorithms/reuse_penalty_portfolio_solvers.h"
#include "dsa/algorithms/reuse_penalty_treewidth_solver.h"
#include "dsa/algorithms/tvm_hill_climb_solver.h"
#include "dsa/algorithms/xla_heap_solver.h"
#include "dsa/analysis/reuse_geometry.h"
#include "dsa/io/minimalloc_csv.h"
#include "dsa/model/architecture.h"
#include "dsa/model/structured_problem.h"
#include "dsa/model/validator.h"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

bool Contains(const std::vector<std::string>& values, std::string_view expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

bool ContainsSubstring(const std::vector<std::string>& values, std::string_view expected) {
  return std::any_of(values.begin(), values.end(), [&](const std::string& value) {
    return value.find(expected) != std::string::npos;
  });
}

dsa::Buffer MakeBuffer(dsa::BufferId id, std::vector<dsa::Interval> intervals, std::uint64_t size) {
  dsa::Buffer buffer;
  buffer.id = id;
  buffer.name = "b" + std::to_string(id);
  buffer.size = size;
  buffer.alignment = 1;
  buffer.live_intervals = std::move(intervals);
  buffer.allowed_pools = {dsa::kDefaultPool};
  return buffer;
}

template <typename T>
const T& ValueOrThrow(const std::optional<T>& value, std::string_view message) {
  if (!value.has_value()) throw std::runtime_error(std::string(message));
  return value.value();
}

const dsa::DsaSolution& SolutionOrThrow(const dsa::DsaResult& result) {
  return ValueOrThrow(result.solution, "solver returned no solution");
}

std::uint64_t OffsetOf(const dsa::DsaResult& result, dsa::BufferId id) {
  const dsa::Placement* placement = SolutionOrThrow(result).Find(id);
  if (placement == nullptr) throw std::runtime_error("solution has no requested placement");
  return placement->offset;
}

std::uint64_t BruteForceMinimumReuseCost(const dsa::DsaProblem& problem) {
  Require(problem.pools.size() == 1 && problem.pools.front().capacity.has_value(),
          "brute-force test helper requires one bounded pool");
  const std::uint64_t capacity = *problem.pools.front().capacity;
  dsa::DsaSolution candidate;
  std::uint64_t best = std::numeric_limits<std::uint64_t>::max();
  std::function<void(std::size_t)> search = [&](std::size_t index) {
    if (index == problem.buffers.size()) {
      if (!dsa::ValidateSolution(problem, candidate).empty()) return;
      best = std::min(best, dsa::EvaluateObjective(problem, candidate).reuse_cost);
      return;
    }
    const dsa::Buffer& buffer = problem.buffers[index];
    if (buffer.size > capacity) return;
    for (std::uint64_t offset = 0; offset <= capacity - buffer.size; ++offset) {
      if (offset % buffer.alignment != 0) continue;
      candidate.placements[buffer.id] = {dsa::kDefaultPool, offset};
      search(index + 1);
    }
    candidate.placements.erase(buffer.id);
  };
  search(0);
  return best;
}

dsa::DsaResult SolveAndValidate(const dsa::DsaProblem& problem, const dsa::DsaSolver& solver) {
  dsa::DsaResult result = solver.Solve(problem);
  std::string status_message =
      std::string("solver did not find a feasible solution: ") + dsa::ToString(result.status);
  if (!result.diagnostics.empty()) status_message += " (" + result.diagnostics.front() + ")";
  Require(result.status == dsa::SolveStatus::kFeasible, status_message);
  const std::vector<std::string> errors = dsa::ValidateSolution(problem, SolutionOrThrow(result));
  Require(errors.empty(), errors.empty() ? "" : errors.front());
  return result;
}

void TestMiniMallocCsv() {
  std::istringstream input(
      "id,lower,upper,size,offset\n"
      "b1,0,3,4,8\n"
      "b2,3,9,4,8\n"
      "b3,0,9,4,4\n"
      "b4,9,21,4,4\n"
      "b5,0,21,4,0\n");
  dsa::MiniMallocDocument document = dsa::ReadMiniMallocCsv(input);
  Require(document.problem.buffers.size() == 5, "CSV buffer count mismatch");
  const dsa::DsaSolution& solution =
      ValueOrThrow(document.solution, "CSV offset column was not loaded");
  Require(dsa::ValidateSolution(document.problem, solution).empty(),
          "official MiniMalloc example solution should validate");
  Require(dsa::EvaluateObjective(document.problem, solution).max_peak == 12,
          "official MiniMalloc example height should be 12");

  std::ostringstream output;
  dsa::WriteMiniMallocCsv(output, document.problem, &solution);
  std::istringstream roundtrip_input(output.str());
  const dsa::MiniMallocDocument roundtrip = dsa::ReadMiniMallocCsv(roundtrip_input);
  const dsa::DsaSolution& roundtrip_solution =
      ValueOrThrow(roundtrip.solution, "solution CSV round-trip lost offsets");
  Require(dsa::EvaluateObjective(roundtrip.problem, roundtrip_solution).max_peak == 12,
          "solution CSV round-trip changed height");

  dsa::StructuredProblemDocument standard;
  standard.profile = dsa::BenchmarkProfile::kStandardDsa;
  standard.instance = "minimalloc_example";
  standard.problem = roundtrip.problem;
  Require(dsa::ValidateStructuredProblemDocument(standard).empty(),
          "MiniMalloc input should satisfy the standard profile");
  standard.problem.separations.push_back({0, 1});
  Require(!dsa::ValidateStructuredProblemDocument(standard).empty(),
          "standard profile silently accepted a compiler-only constraint");
  standard.problem.separations.clear();
  dsa::PyptoStructure structure;
  structure.alias_classes.push_back({0, {"b1"}});
  standard.problem.pypto_structure = std::move(structure);
  Require(!dsa::ValidateStructuredProblemDocument(standard).empty(),
          "standard profile silently accepted PyPTO provenance");
}

void TestFirstFitSubdividesFreedRegion() {
  constexpr std::uint64_t kKiB = 1024;
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{0, 3}}, 64 * kKiB),
      MakeBuffer(1, {{3, 6}}, 32 * kKiB),
      MakeBuffer(2, {{3, 6}}, 32 * kKiB),
  };
  problem.pools.front().capacity = 64 * kKiB;

  const dsa::DsaResult result = SolveAndValidate(problem, dsa::FirstFitSolver());
  Require(result.objective.max_peak == 64 * kKiB,
          "#1908 shape did not fit the freed producer region");
  const std::uint64_t first = OffsetOf(result, 1);
  const std::uint64_t second = OffsetOf(result, 2);
  Require((first == 0 && second == 32 * kKiB) || (second == 0 && first == 32 * kKiB),
          "consumers did not subdivide the freed producer region");
}

void TestMultiIntervalLiveness() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{0, 2}, {4, 6}}, 32),
      MakeBuffer(1, {{2, 4}}, 32),
      MakeBuffer(2, {{1, 3}}, 32),
  };
  const dsa::DsaResult result = SolveAndValidate(problem, dsa::FirstFitSolver());
  Require(OffsetOf(result, 0) == OffsetOf(result, 1),
          "touching half-open multi-interval lifetimes should reuse an address");
  Require(OffsetOf(result, 0) != OffsetOf(result, 2),
          "overlapping multi-interval lifetimes shared an address");
}

void TestHardConstraintsAndPinnedRanges() {
  dsa::DsaProblem problem;
  problem.pools.front().reserved_ranges.push_back({0, 16});
  problem.buffers = {
      MakeBuffer(0, {{0, 2}}, 16),
      MakeBuffer(1, {{2, 4}}, 16),
      MakeBuffer(2, {{0, 4}}, 16),
  };
  for (dsa::Buffer& buffer : problem.buffers) buffer.alignment = 16;
  problem.separations.push_back({0, 1});
  problem.pinned_allocations.push_back({2, dsa::kDefaultPool, 32, true});
  const dsa::DsaResult result = SolveAndValidate(problem, dsa::FirstFitSolver());
  Require(OffsetOf(result, 2) == 32, "pinned offset was not preserved");
  Require(OffsetOf(result, 0) == 16, "reserved range was not avoided");
  Require(OffsetOf(result, 1) != OffsetOf(result, 0),
          "separated disjoint-lifetime buffers shared an address");
  Require(result.objective.max_peak == 64, "reserved/pinned objective height mismatch");
}

void TestColocation() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{0, 4}}, 32),
      MakeBuffer(1, {{0, 4}}, 16),
  };
  problem.colocations.push_back({0, 1});
  const dsa::DsaResult result = SolveAndValidate(problem, dsa::FirstFitSolver());
  Require(OffsetOf(result, 0) == OffsetOf(result, 1), "colocated buffers did not share an offset");
  Require(result.objective.max_peak == 32, "colocation class should use its largest member size");
}

void TestTemporalExclusionOverridesConservativeHulls() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{0, 10}}, 32),
      MakeBuffer(1, {{2, 8}}, 32),
  };
  problem.temporal_exclusions.push_back({0, 1});
  const dsa::DsaResult result = SolveAndValidate(problem, dsa::FirstFitSolver());
  Require(OffsetOf(result, 0) == OffsetOf(result, 1),
          "branch-exclusive buffers did not reuse an address");
  Require(result.objective.max_peak == 32, "branch-exclusive conservative hulls inflated peak");
}

void TestFixedPoolsAreIndependent() {
  dsa::DsaProblem problem;
  problem.pools = {{0, "left", 64, {}, std::nullopt}, {1, "right", 64, {}, std::nullopt}};
  problem.buffers = {
      MakeBuffer(0, {{0, 4}}, 32),
      MakeBuffer(1, {{0, 4}}, 48),
  };
  problem.buffers[1].allowed_pools = {1};
  const dsa::DsaResult result = SolveAndValidate(problem, dsa::FirstFitSolver());
  Require(OffsetOf(result, 0) == 0 && OffsetOf(result, 1) == 0,
          "fixed pools did not allocate independently");
  Require(result.objective.peak_by_pool.at(0) == 32 && result.objective.peak_by_pool.at(1) == 48,
          "per-pool peaks are incorrect");
}

void TestFitCostObjectiveAvoidsExpensiveReuse() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{2, 9}}, 5), MakeBuffer(1, {{4, 8}}, 3), MakeBuffer(2, {{7, 10}}, 3),
      MakeBuffer(3, {{8, 9}}, 2), MakeBuffer(4, {{0, 6}}, 6),
  };
  problem.pools.front().capacity = 14;
  dsa::CostModel cost_model;
  cost_model.reuse_penalties.push_back({3, 4, 100, dsa::ReusePenaltyReason::kCrossPipe});
  problem.cost_model = std::move(cost_model);

  const dsa::DsaResult baseline = SolveAndValidate(problem, dsa::FirstFitSolver());
  Require(baseline.objective.max_peak == 14 && baseline.objective.reuse_cost == 100,
          "reuse-cost witness no longer exercises the expensive baseline reuse");
  problem.objective = dsa::FitThenMinimizeReuseCostObjective();

  dsa::LocalSearchOptions options;
  options.seed = 11;
  options.max_iterations = 2'000;
  options.restarts = 4;
  options.stagnation_limit = 100;
  const dsa::DsaResult result = SolveAndValidate(problem, dsa::LocalSearchSolver(options));
  Require(result.objective.max_peak <= 14 && result.objective.reuse_cost == 0,
          "fit-cost local search did not avoid the expensive reuse edge");
}

void TestDsaRpConstructiveBaselines() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{0, 1}}, 1),
      MakeBuffer(1, {{1, 2}}, 1),
      MakeBuffer(2, {{2, 3}}, 1),
  };
  problem.pools.front().capacity = 2;
  problem.cost_model = dsa::CostModel{{
      {0, 1, 9, dsa::ReusePenaltyReason::kCrossPipe},
      {1, 2, 4, dsa::ReusePenaltyReason::kCrossPipe},
  }};
  problem.objective = dsa::FitThenMinimizeReuseCostObjective();

  const dsa::DsaResult first_fit = SolveAndValidate(problem, dsa::FirstFitSolver());
  Require(first_fit.objective.reuse_cost == 13,
          "first-fit control no longer exposes penalty-blind reuse");

  const dsa::DsaResult canonical = SolveAndValidate(problem, dsa::CanonicalGreedySolver());
  Require(canonical.objective.reuse_cost == BruteForceMinimumReuseCost(problem) &&
              canonical.objective.reuse_cost == 0,
          "canonical greedy missed the zero-cost support placement");
  Require(OffsetOf(canonical, 0) == OffsetOf(canonical, 2) &&
              OffsetOf(canonical, 0) != OffsetOf(canonical, 1),
          "canonical greedy did not select the expected two-color placement");
  Require(canonical.solver_metrics.at("candidate_offsets_evaluated") > 3,
          "canonical greedy did not evaluate its extended support menu");

  const dsa::DsaResult promote = SolveAndValidate(problem, dsa::PromoteRepairSolver());
  Require(promote.objective.reuse_cost == BruteForceMinimumReuseCost(problem) &&
              promote.objective.reuse_cost == 0,
          "promote-repair did not retain a feasible zero-penalty placement");
  Require(promote.solver_metrics.at("demoted_edges") == 0 &&
              promote.solver_metrics.at("active_soft_edges") == 2,
          "promote-repair unexpectedly relaxed a fitting promoted problem");

  problem.pools.front().capacity = 1;
  const std::uint64_t optimum = BruteForceMinimumReuseCost(problem);
  Require(optimum == 13, "capacity-one fixture has the wrong brute-force optimum");
  const dsa::DsaResult repaired = SolveAndValidate(problem, dsa::PromoteRepairSolver());
  Require(repaired.objective.reuse_cost == optimum,
          "promote-repair did not pay the forced capacity-one penalty");
  Require(repaired.solver_metrics.at("demoted_edges") == 2 &&
              repaired.solver_metrics.at("packing_attempts") == 3,
          "promote-repair did not report its two support-chain demotions");
  const dsa::DsaResult constrained = SolveAndValidate(problem, dsa::CanonicalGreedySolver());
  Require(constrained.objective.reuse_cost == optimum,
          "canonical greedy did not return the capacity-one optimum");

  problem.pools.front().capacity = 2;
  problem.cost_model->reuse_penalties.push_back({0, 2, 1, dsa::ReusePenaltyReason::kCrossPipe});
  const std::uint64_t triangle_optimum = BruteForceMinimumReuseCost(problem);
  Require(triangle_optimum == 1, "soft-triangle fixture has the wrong brute-force optimum");
  const dsa::DsaResult triangle_canonical = SolveAndValidate(problem, dsa::CanonicalGreedySolver());
  Require(triangle_canonical.objective.reuse_cost == triangle_optimum,
          "canonical greedy missed the cheapest overlap in the soft triangle");
  const dsa::DsaResult triangle_repair = SolveAndValidate(problem, dsa::PromoteRepairSolver());
  Require(triangle_repair.objective.reuse_cost == 4,
          "promote-repair did not demote the cheapest edge on its support chain");
  Require(triangle_repair.solver_metrics.at("demoted_edges") == 1 &&
              triangle_repair.objective.reuse_cost > triangle_optimum,
          "promote-repair fixture no longer exposes the heuristic optimality gap");

  const dsa::DsaResult strict = dsa::PromoteAllSolver().Solve(problem);
  Require(strict.status == dsa::SolveStatus::kBestEffortNoFit &&
              strict.solver_metrics.at("promoted_edges") == 3,
          "promote-all control did not report the strict capacity failure");

  dsa::UnitRandomColoringOptions random_options;
  random_options.seed = 23;
  random_options.samples = 8;
  const dsa::DsaResult random =
      SolveAndValidate(problem, dsa::UnitRandomColoringSolver(random_options));
  Require(random.solver_metrics.at("samples") == 8 && random.objective.max_peak <= 2,
          "unit random-coloring control did not produce a sampled fit");
}

void TestDsaRpExactSolvers() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{0, 1}}, 1),
      MakeBuffer(1, {{1, 2}}, 1),
      MakeBuffer(2, {{2, 3}}, 1),
  };
  problem.pools.front().capacity = 2;
  problem.cost_model = dsa::CostModel{{
      {0, 1, 9, dsa::ReusePenaltyReason::kCrossPipe},
      {1, 2, 4, dsa::ReusePenaltyReason::kCrossPipe},
      {0, 2, 1, dsa::ReusePenaltyReason::kCrossPipe},
  }};
  problem.objective = dsa::FitThenMinimizeReuseCostObjective();
  const std::uint64_t optimum = BruteForceMinimumReuseCost(problem);
  Require(optimum == 1, "exact-solver fixture has the wrong brute-force optimum");

  const dsa::DsaResult branch_and_bound =
      SolveAndValidate(problem, dsa::CanonicalBranchAndBoundSolver());
  Require(branch_and_bound.objective.reuse_cost == optimum,
          "canonical branch-and-bound disagrees with brute force");
  Require(branch_and_bound.solver_metrics.at("optimality_proven") == 1,
          "canonical branch-and-bound did not certify its result");

  const dsa::DsaResult hitting_set = SolveAndValidate(problem, dsa::ImplicitHittingSetSolver());
  Require(hitting_set.objective.reuse_cost == optimum,
          "implicit hitting set disagrees with brute force");
  Require(hitting_set.solver_metrics.at("optimal_reuse_cost_proven") == 1 &&
              hitting_set.solver_metrics.at("verified_cores") > 0,
          "implicit hitting set did not expose its proof metrics");
  const dsa::DsaResult capacity_two = SolveAndValidate(problem, dsa::CapacityTwoExactSolver());
  Require(capacity_two.objective.reuse_cost == optimum &&
              capacity_two.solver_metrics.at("optimality_proven") == 1,
          "capacity-two specialization disagrees with brute force");
  const dsa::DsaResult treewidth = SolveAndValidate(problem, dsa::TreewidthPartitionDpSolver());
  Require(treewidth.objective.reuse_cost == optimum &&
              treewidth.solver_metrics.at("optimal_reuse_cost_proven") == 1,
          "treewidth specialization disagrees with brute force");
  const dsa::DsaResult portfolio = SolveAndValidate(problem, dsa::ReusePenaltyPortfolioSolver());
  Require(portfolio.objective.reuse_cost == optimum &&
              portfolio.solver_metrics.at("portfolio_method") == 2,
          "portfolio did not dispatch the capacity-two fixture correctly");

  dsa::DsaProblem infeasible;
  infeasible.buffers = {
      MakeBuffer(0, {{0, 2}}, 1),
      MakeBuffer(1, {{0, 2}}, 1),
      MakeBuffer(2, {{0, 2}}, 1),
  };
  infeasible.pools.front().capacity = 2;
  infeasible.objective = dsa::FitThenMinimizeReuseCostObjective();
  const dsa::DsaResult cbb_infeasible = dsa::CanonicalBranchAndBoundSolver().Solve(infeasible);
  Require(cbb_infeasible.status == dsa::SolveStatus::kInfeasibleProven,
          "canonical branch-and-bound missed hard infeasibility");
  const dsa::DsaResult ihs_infeasible = dsa::ImplicitHittingSetSolver().Solve(infeasible);
  Require(ihs_infeasible.status == dsa::SolveStatus::kInfeasibleProven,
          "implicit hitting set missed hard infeasibility");
  const dsa::DsaResult capacity_two_infeasible = dsa::CapacityTwoExactSolver().Solve(infeasible);
  Require(capacity_two_infeasible.status == dsa::SolveStatus::kInfeasibleProven,
          "capacity-two specialization missed hard infeasibility");

  std::mt19937_64 random(0xD5A2026U);
  for (std::size_t instance = 0; instance < 24; ++instance) {
    dsa::DsaProblem generated;
    generated.pools.front().capacity = 3 + random() % 3;
    for (dsa::BufferId id = 0; id < 4; ++id) {
      const std::int64_t begin = static_cast<std::int64_t>(random() % 4);
      const std::int64_t end = begin + 1 + static_cast<std::int64_t>(random() % 2);
      generated.buffers.push_back(MakeBuffer(id, {{begin, end}}, 1 + random() % 2));
    }
    dsa::CostModel costs;
    for (std::size_t first = 0; first < generated.buffers.size(); ++first) {
      for (std::size_t second = first + 1; second < generated.buffers.size(); ++second) {
        if (!generated.buffers[first].live_intervals.front().Overlaps(
                generated.buffers[second].live_intervals.front()) &&
            random() % 4 != 0) {
          costs.reuse_penalties.push_back({generated.buffers[first].id,
                                           generated.buffers[second].id, 1 + random() % 9,
                                           dsa::ReusePenaltyReason::kCrossPipe});
        }
      }
    }
    generated.cost_model = std::move(costs);
    generated.objective = dsa::FitThenMinimizeReuseCostObjective();
    const std::uint64_t generated_optimum = BruteForceMinimumReuseCost(generated);
    const dsa::DsaResult generated_cbb = dsa::CanonicalBranchAndBoundSolver().Solve(generated);
    const dsa::DsaResult generated_ihs = dsa::ImplicitHittingSetSolver().Solve(generated);
    if (generated_optimum == std::numeric_limits<std::uint64_t>::max()) {
      Require(generated_cbb.status == dsa::SolveStatus::kInfeasibleProven &&
                  generated_ihs.status == dsa::SolveStatus::kInfeasibleProven,
              "exact solvers disagreed with brute-force infeasibility");
    } else {
      Require(generated_cbb.status == dsa::SolveStatus::kFeasible &&
                  generated_cbb.objective.reuse_cost == generated_optimum,
              "canonical branch-and-bound failed generated cross-check");
      Require(generated_ihs.status == dsa::SolveStatus::kFeasible &&
                  generated_ihs.objective.reuse_cost == generated_optimum,
              "implicit hitting set failed generated cross-check");
    }
  }

  for (std::size_t instance = 0; instance < 24; ++instance) {
    dsa::DsaProblem generated;
    generated.pools.front().capacity = 2;
    for (dsa::BufferId id = 0; id < 5; ++id) {
      const std::int64_t begin = static_cast<std::int64_t>(random() % 5);
      const std::int64_t end = begin + 1 + static_cast<std::int64_t>(random() % 2);
      generated.buffers.push_back(MakeBuffer(id, {{begin, end}}, 1));
    }
    dsa::CostModel costs;
    for (std::size_t first = 0; first < generated.buffers.size(); ++first) {
      for (std::size_t second = first + 1; second < generated.buffers.size(); ++second) {
        if (!generated.buffers[first].live_intervals.front().Overlaps(
                generated.buffers[second].live_intervals.front()) &&
            random() % 3 != 0) {
          costs.reuse_penalties.push_back({generated.buffers[first].id,
                                           generated.buffers[second].id, 1 + random() % 7,
                                           dsa::ReusePenaltyReason::kCrossPipe});
        }
      }
    }
    generated.cost_model = std::move(costs);
    generated.objective = dsa::FitThenMinimizeReuseCostObjective();
    const std::uint64_t generated_optimum = BruteForceMinimumReuseCost(generated);
    const dsa::DsaResult specialized = dsa::CapacityTwoExactSolver().Solve(generated);
    const dsa::DsaResult treewidth_specialized = dsa::TreewidthPartitionDpSolver().Solve(generated);
    if (generated_optimum == std::numeric_limits<std::uint64_t>::max()) {
      Require(specialized.status == dsa::SolveStatus::kInfeasibleProven,
              "capacity-two specialization disagreed with generated "
              "infeasibility");
      Require(treewidth_specialized.status == dsa::SolveStatus::kInfeasibleProven,
              "treewidth specialization disagreed with generated "
              "infeasibility");
    } else {
      Require(specialized.status == dsa::SolveStatus::kFeasible &&
                  specialized.objective.reuse_cost == generated_optimum,
              "capacity-two specialization failed generated cross-check");
      Require(treewidth_specialized.status == dsa::SolveStatus::kFeasible &&
                  treewidth_specialized.objective.reuse_cost == generated_optimum,
              "treewidth specialization failed generated cross-check");
    }
  }

  dsa::DsaProblem phases;
  phases.pools.front().capacity = 3;
  phases.buffers = {
      MakeBuffer(0, {{0, 1}}, 1), MakeBuffer(1, {{0, 1}}, 1), MakeBuffer(2, {{2, 3}}, 1),
      MakeBuffer(3, {{2, 3}}, 1), MakeBuffer(4, {{2, 3}}, 1), MakeBuffer(5, {{4, 5}}, 1),
      MakeBuffer(6, {{4, 5}}, 1),
  };
  phases.cost_model = dsa::CostModel{{
      {0, 2, 9, dsa::ReusePenaltyReason::kCrossPipe},
      {0, 3, 1, dsa::ReusePenaltyReason::kCrossPipe},
      {1, 3, 8, dsa::ReusePenaltyReason::kCrossPipe},
      {1, 4, 2, dsa::ReusePenaltyReason::kCrossPipe},
      {2, 5, 7, dsa::ReusePenaltyReason::kCrossPipe},
      {3, 5, 3, dsa::ReusePenaltyReason::kCrossPipe},
      {4, 6, 4, dsa::ReusePenaltyReason::kCrossPipe},
  }};
  phases.objective = dsa::FitThenMinimizeReuseCostObjective();
  const std::uint64_t phase_optimum = BruteForceMinimumReuseCost(phases);
  const dsa::DsaResult flow = SolveAndValidate(phases, dsa::SpanOneMinCostFlowSolver());
  Require(flow.objective.reuse_cost == phase_optimum &&
              flow.solver_metrics.at("optimal_reuse_cost_proven") == 1,
          "span-one flow specialization disagrees with brute force");
  const dsa::DsaResult phase_treewidth =
      SolveAndValidate(phases, dsa::TreewidthPartitionDpSolver());
  Require(phase_treewidth.objective.reuse_cost == phase_optimum,
          "treewidth specialization disagrees on the span-one fixture");
  const dsa::DsaResult phase_portfolio =
      SolveAndValidate(phases, dsa::ReusePenaltyPortfolioSolver());
  Require(phase_portfolio.objective.reuse_cost == phase_optimum &&
              phase_portfolio.solver_metrics.at("portfolio_method") == 1,
          "portfolio did not dispatch the span-one fixture correctly");

  dsa::DsaProblem treewidth_only = problem;
  treewidth_only.pools.front().capacity = 3;
  const dsa::DsaResult treewidth_portfolio =
      SolveAndValidate(treewidth_only, dsa::ReusePenaltyPortfolioSolver());
  Require(treewidth_portfolio.objective.reuse_cost == 0 &&
              treewidth_portfolio.solver_metrics.at("portfolio_method") == 3,
          "portfolio did not dispatch the bounded-treewidth fixture "
          "correctly");

  for (std::size_t instance = 0; instance < 16; ++instance) {
    dsa::DsaProblem generated;
    generated.pools.front().capacity = 3;
    dsa::BufferId next_id = 0;
    std::vector<std::vector<dsa::BufferId>> generated_phases;
    for (std::size_t phase = 0; phase < 3; ++phase) {
      generated_phases.emplace_back();
      const std::size_t phase_size = 1 + static_cast<std::size_t>(random() % 3);
      for (std::size_t member = 0; member < phase_size; ++member) {
        generated.buffers.push_back(MakeBuffer(
            next_id,
            {{static_cast<std::int64_t>(phase * 2), static_cast<std::int64_t>(phase * 2 + 1)}}, 1));
        generated_phases.back().push_back(next_id++);
      }
    }
    dsa::CostModel costs;
    for (std::size_t phase = 0; phase + 1 < generated_phases.size(); ++phase) {
      for (dsa::BufferId first : generated_phases[phase]) {
        for (dsa::BufferId second : generated_phases[phase + 1]) {
          if (random() % 4 != 0) {
            costs.reuse_penalties.push_back(
                {first, second, 1 + random() % 11, dsa::ReusePenaltyReason::kCrossPipe});
          }
        }
      }
    }
    generated.cost_model = std::move(costs);
    generated.objective = dsa::FitThenMinimizeReuseCostObjective();
    const std::uint64_t generated_optimum = BruteForceMinimumReuseCost(generated);
    const dsa::DsaResult specialized = SolveAndValidate(generated, dsa::SpanOneMinCostFlowSolver());
    Require(specialized.objective.reuse_cost == generated_optimum,
            "span-one flow specialization failed generated cross-check");
    const dsa::DsaResult generated_treewidth =
        SolveAndValidate(generated, dsa::TreewidthPartitionDpSolver());
    Require(generated_treewidth.objective.reuse_cost == generated_optimum,
            "treewidth specialization failed generated phase cross-check");
  }
}

void TestDsaRpPromotedSetLocalSearch() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{0, 1}}, 1),
      MakeBuffer(1, {{1, 2}}, 1),
      MakeBuffer(2, {{2, 3}}, 1),
  };
  problem.pools.front().capacity = 2;
  problem.cost_model = dsa::CostModel{{
      {0, 1, 9, dsa::ReusePenaltyReason::kCrossPipe},
      {1, 2, 4, dsa::ReusePenaltyReason::kCrossPipe},
      {0, 2, 1, dsa::ReusePenaltyReason::kCrossPipe},
  }};
  problem.objective = dsa::FitThenMinimizeReuseCostObjective();

  dsa::ReusePenaltyLocalSearchOptions options;
  options.seed = 17;
  options.max_evaluations = 256;
  options.restarts = 2;
  options.stagnation_limit = 8;
  options.soft_moves_per_iteration = 8;
  options.order_moves_per_iteration = 4;
  const dsa::DsaResult result =
      SolveAndValidate(problem, dsa::ReusePenaltyLocalSearchSolver(options));
  Require(result.objective.reuse_cost == BruteForceMinimumReuseCost(problem) &&
              result.objective.reuse_cost == 1,
          "promoted-set local search missed the soft-triangle optimum");
  Require(
      result.solver_metrics.at("soft_moves") > 0 && result.solver_metrics.at("promoted_edges") == 2,
      "promoted-set local search did not expose its soft-edge state");

  problem.pools.front().capacity = 1;
  const dsa::DsaResult forced =
      SolveAndValidate(problem, dsa::ReusePenaltyLocalSearchSolver(options));
  Require(forced.objective.reuse_cost == 14,
          "promoted-set local search mishandled forced full reuse");
}

void TestSparseReferenceReducesPhysicalReuse() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{0, 2}}, 10),
      MakeBuffer(1, {{2, 4}}, 10),
      MakeBuffer(2, {{0, 4}}, 10),
  };
  problem.pools.front().capacity = 30;
  dsa::DsaSolution compact;
  compact.placements = {
      {0, {dsa::kDefaultPool, 0}},
      {1, {dsa::kDefaultPool, 0}},
      {2, {dsa::kDefaultPool, 10}},
  };
  Require(dsa::ValidateSolution(problem, compact).empty(), "sparse-reference fixture is invalid");
  const dsa::ReuseGeometryStats compact_stats = dsa::EvaluateReuseGeometry(problem, compact);
  Require(compact_stats.pair_count == 1 && compact_stats.overlap_bytes == 10,
          "compact fixture has the wrong reuse geometry");

  const dsa::SparseReferenceResult sparse = dsa::BuildSparseReferencePlacement(problem, compact);
  Require(dsa::ValidateSolution(problem, sparse.solution).empty(), "sparse reference is invalid");
  Require(sparse.final.pair_count == 0 && sparse.final.overlap_bytes == 0,
          "sparse reference did not eliminate avoidable physical reuse");
  Require(dsa::EvaluateObjective(problem, sparse.solution).max_peak == 30,
          "sparse reference did not use the available capacity");
}

void TestStructuredSolutionRoundTripAndMismatchRejection() {
  dsa::StructuredProblemDocument problem;
  problem.profile = dsa::BenchmarkProfile::kPyptoHardV1;
  problem.instance = "solution_roundtrip";
  problem.metadata = {
      {"lifetime_ordering", "pypto_read_before_write"},
      {"solver_input", "pre_memory_reuse"},
  };
  problem.problem.buffers = {
      MakeBuffer(0, {{0, 2}}, 8),
      MakeBuffer(1, {{2, 4}}, 8),
  };
  const dsa::DsaResult result = SolveAndValidate(problem.problem, dsa::FirstFitSolver());
  Require(result.solution.has_value(), "solution round-trip test has no placement");

  const dsa::StructuredSolutionDocument solution =
      dsa::BuildStructuredSolutionDocument(problem, *result.solution, {{"solver", "first_fit"}});
  std::stringstream encoded;
  dsa::WriteStructuredSolutionJson(encoded, solution);
  const dsa::StructuredSolutionDocument decoded = dsa::ReadStructuredSolutionJson(encoded);
  const dsa::DsaSolution replayed = dsa::ValidateAndExtractStructuredSolution(problem, decoded);
  Require(replayed.placements == result.solution->placements,
          "structured solution round-trip changed placements");

  dsa::StructuredProblemDocument changed = problem;
  changed.problem.buffers.front().size = 16;
  bool rejected = false;
  try {
    static_cast<void>(dsa::ValidateAndExtractStructuredSolution(changed, decoded));
  } catch (const std::invalid_argument& error) {
    rejected = std::string(error.what()).find("fingerprint") != std::string::npos;
  }
  Require(rejected, "structured solution replay accepted a different problem");
}

void TestPipelineIntentRelaxationPreservesNonPipelineReasons() {
  dsa::StructuredProblemDocument document;
  document.profile = dsa::BenchmarkProfile::kPyptoHardV1;
  document.instance = "pipeline_intent_relaxation";
  document.metadata = {
      {"lifetime_ordering", "pypto_read_before_write"},
      {"solver_input", "pre_memory_reuse"},
  };
  document.problem.buffers = {
      MakeBuffer(0, {{0, 2}}, 8),
      MakeBuffer(1, {{2, 4}}, 8),
      MakeBuffer(2, {{4, 6}}, 8),
  };
  document.problem.separations = {
      {0, 1, {dsa::SeparationReason::kPipelineStage}},
      {1, 2, {dsa::SeparationReason::kPipelineStage, dsa::SeparationReason::kTargetHazard}},
  };
  document.problem.pypto_structure = dsa::PyptoStructure{};

  const dsa::PipelineIntentRelaxation relaxation = dsa::BuildPipelineIntentRelaxation(document, 7);
  Require(relaxation.removed_pipeline_reason_count == 2,
          "pipeline relaxation did not count both pipeline-stage reasons");
  Require(relaxation.relaxed_separation_count == 1,
          "pipeline relaxation counted a pair that remains hard for another reason");
  Require(relaxation.added_penalty_count == 1,
          "pipeline relaxation should add a cost only for the fully relaxed pair");
  Require(relaxation.document.profile == dsa::BenchmarkProfile::kPyptoResearchV1,
          "pipeline relaxation did not mark the document research-only");
  Require(relaxation.document.problem.separations.size() == 1 &&
              relaxation.document.problem.separations.front().first == 1 &&
              relaxation.document.problem.separations.front().second == 2 &&
              relaxation.document.problem.separations.front().reasons ==
                  std::vector<dsa::SeparationReason>{dsa::SeparationReason::kTargetHazard},
          "pipeline relaxation weakened a non-pipeline hard reason");
  Require(relaxation.document.problem.cost_model &&
              relaxation.document.problem.cost_model->reuse_penalties.size() == 1,
          "pipeline relaxation did not create the expected sparse reuse cost");
  const dsa::ReusePenalty& penalty =
      relaxation.document.problem.cost_model->reuse_penalties.front();
  Require(penalty.first == 0 && penalty.second == 1 && penalty.cost == 7 &&
              penalty.reason == dsa::ReusePenaltyReason::kPipelineSerialization,
          "pipeline relaxation produced the wrong reuse penalty");
  Require(
      relaxation.document.problem.objective.terms == dsa::FitThenMinimizeReuseCostObjective().terms,
      "pipeline relaxation did not select the fit-then-reuse objective");
  Require(dsa::ValidateStructuredProblemDocument(relaxation.document).empty(),
          "pipeline relaxation produced an invalid document");
}

void TestLocalSearchClosesOrderingGap() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{5, 7}}, 5), MakeBuffer(1, {{0, 7}}, 2), MakeBuffer(2, {{4, 10}}, 2),
      MakeBuffer(3, {{4, 6}}, 2), MakeBuffer(4, {{2, 4}}, 6),
  };
  const dsa::DsaResult baseline = SolveAndValidate(problem, dsa::FirstFitSolver());
  Require(baseline.objective.max_peak == 12, "first-fit witness no longer exposes an ordering gap");

  dsa::LocalSearchOptions options;
  options.seed = 7;
  options.max_iterations = 2'000;
  options.restarts = 4;
  options.stagnation_limit = 100;
  const dsa::LocalSearchSolver solver(options);
  const dsa::DsaResult first = SolveAndValidate(problem, solver);
  const dsa::DsaResult second = SolveAndValidate(problem, solver);
  Require(first.objective.max_peak == 11, "local search did not close the first-fit ordering gap");
  Require(SolutionOrThrow(first).placements == SolutionOrThrow(second).placements,
          "local search is not deterministic for a fixed seed");
}

void TestLocalSearchUsesGlobalEvaluationBudget() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{5, 7}}, 5), MakeBuffer(1, {{0, 7}}, 2), MakeBuffer(2, {{4, 10}}, 2),
      MakeBuffer(3, {{4, 6}}, 2), MakeBuffer(4, {{2, 4}}, 6),
  };
  dsa::LocalSearchOptions options;
  options.seed = 7;
  options.max_iterations = 2;
  options.restarts = 100;
  options.stagnation_limit = 1;
  const dsa::DsaResult result = SolveAndValidate(problem, dsa::LocalSearchSolver(options));
  Require(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                      [](const std::string& diagnostic) {
                        return diagnostic.find("local search evaluated 2 placement orders") !=
                               std::string::npos;
                      }),
          "local search did not enforce or report its global decoder budget");
}

void TestTvmHillClimbClosesOrderingGap() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{5, 7}}, 5), MakeBuffer(1, {{0, 7}}, 2), MakeBuffer(2, {{4, 10}}, 2),
      MakeBuffer(3, {{4, 6}}, 2), MakeBuffer(4, {{2, 4}}, 6),
  };
  const dsa::DsaResult baseline = SolveAndValidate(problem, dsa::FirstFitSolver());
  Require(baseline.objective.max_peak == 12,
          "TVM hill-climb witness no longer exposes an ordering gap");

  dsa::TvmHillClimbOptions options;
  options.seed = 7;
  options.max_attempts = 500;
  options.worse_move_scale_percent = 50;
  const dsa::TvmHillClimbSolver solver(options);
  const dsa::DsaResult first = SolveAndValidate(problem, solver);
  const dsa::DsaResult second = SolveAndValidate(problem, solver);
  Require(first.objective.max_peak == 11,
          "TVM-style graph-guided search did not close the ordering gap");
  Require(SolutionOrThrow(first).placements == SolutionOrThrow(second).placements,
          "TVM-style hill climb is not deterministic for a fixed seed");
}

dsa::StructuredProblemDocument MakeStructuredDocument() {
  dsa::StructuredProblemDocument document;
  document.profile = dsa::BenchmarkProfile::kPyptoResearchV1;
  document.instance = "pypto_fixture";
  document.metadata = {{"kernel", "fixture"},
                       {"lifetime_ordering", "pypto_read_before_write"},
                       {"solver_input", "pre_memory_reuse"},
                       {"target", "ascend910b"}};
  document.problem.pools = {
      {3, "L1", 128, {{0, 16}}, dsa::BankGeometry{16, 8}},
      {4, "L0B", 64, {}, std::nullopt},
  };
  document.problem.buffers = {
      MakeBuffer(0, {{0, 2}, {4, 6}}, 32),
      MakeBuffer(1, {{2, 4}}, 16),
      MakeBuffer(2, {{0, 6}}, 16),
  };
  document.problem.buffers[0].alignment = 16;
  document.problem.buffers[0].allowed_pools = {3};
  document.problem.buffers[1].alignment = 16;
  document.problem.buffers[1].allowed_pools = {3};
  document.problem.buffers[2].alignment = 16;
  document.problem.buffers[2].allowed_pools = {4};
  document.problem.separations.push_back({0, 1, {dsa::SeparationReason::kPipelineStage}});
  document.problem.pinned_allocations.push_back({2, 4, 0, false});
  dsa::CostModel cost_model;
  cost_model.reuse_penalties.push_back({0, 1, 25, dsa::ReusePenaltyReason::kPipelineSerialization});
  cost_model.reuse_penalties.push_back(
      {0, 1, 5, dsa::ReusePenaltyReason::kLoadMotionSerialization});
  document.problem.cost_model = std::move(cost_model);
  dsa::PyptoStructure structure;
  structure.alias_classes = {
      {0, {"tile_a", "tile_a_view"}},
      {1, {"tile_b"}},
      {2, {"tile_c"}},
  };
  structure.pipeline_groups = {
      {7, 3, 32, 2, 2, {{0, 0, 0}, {1, 1, 1}}},
  };
  document.problem.pypto_structure = std::move(structure);
  document.problem.objective = dsa::FitThenMinimizeReuseCostObjective();
  return document;
}

void TestStructuredJsonRoundTripAndProfiles() {
  const dsa::StructuredProblemDocument original = MakeStructuredDocument();
  Require(dsa::ValidateStructuredProblemDocument(original).empty(),
          "structured fixture should validate");

  std::ostringstream output;
  dsa::WriteStructuredProblemJson(output, original);
  std::istringstream input(output.str());
  const dsa::StructuredProblemDocument parsed = dsa::ReadStructuredProblemJson(input);
  Require(parsed.schema_version == dsa::kStructuredProblemSchemaVersion,
          "structured schema version did not round-trip");
  Require(parsed.profile == dsa::BenchmarkProfile::kPyptoResearchV1,
          "structured profile did not round-trip");
  Require(parsed.problem.buffers.size() == 3 && parsed.problem.pools.size() == 2,
          "structured core did not round-trip");
  Require(parsed.problem.buffers.front().live_intervals.size() == 2,
          "multi-interval liveness did not round-trip");
  Require(parsed.problem.separations.size() == 1 && parsed.problem.pinned_allocations.size() == 1,
          "structured constraints did not round-trip");
  Require(parsed.problem.separations.front().reasons ==
              std::vector<dsa::SeparationReason>{dsa::SeparationReason::kPipelineStage},
          "separation provenance did not round-trip");
  Require(parsed.problem.pypto_structure &&
              parsed.problem.pypto_structure->alias_classes.front().members.size() == 2 &&
              parsed.problem.pypto_structure->pipeline_groups.front().effective_depth == 2,
          "normalized PyPTO structure did not round-trip");
  Require(parsed.problem.cost_model && parsed.problem.cost_model->reuse_penalties.size() == 2 &&
              parsed.problem.cost_model->reuse_penalties.front().cost == 25 &&
              parsed.problem.cost_model->reuse_penalties.back().reason ==
                  dsa::ReusePenaltyReason::kLoadMotionSerialization,
          "structured cost model did not round-trip");
  Require(parsed.problem.objective.terms == dsa::FitThenMinimizeReuseCostObjective().terms,
          "structured objective did not round-trip");

  dsa::StructuredProblemDocument falsely_hard = original;
  falsely_hard.profile = dsa::BenchmarkProfile::kPyptoHardV1;
  Require(!dsa::ValidateStructuredProblemDocument(falsely_hard).empty(),
          "pypto_hard_v1 silently accepted research-only features");

  dsa::StructuredProblemDocument invalid_structure = original;
  invalid_structure.problem.pypto_structure->pipeline_groups.front().effective_depth = 3;
  Require(!dsa::ValidateStructuredProblemDocument(invalid_structure).empty(),
          "invalid PyPTO pipeline depth was silently accepted");

  std::string unknown_field = output.str();
  unknown_field.insert(unknown_field.find('{') + 1, "\n  \"unexpected\": true,");
  std::istringstream unknown_input(unknown_field);
  bool rejected = false;
  try {
    static_cast<void>(dsa::ReadStructuredProblemJson(unknown_input));
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  Require(rejected, "schema-v1 reader silently accepted an unknown field");
}

void TestPyptoHardV1RejectsResearchFeatures() {
  const std::filesystem::path path = std::filesystem::path(DSA_TEST_SOURCE_DIR) / "tests" / "data" /
                                     "chain_read_before_write_v1.json";
  const dsa::StructuredProblemDocument hard = dsa::ReadStructuredProblemJsonFile(path);
  Require(hard.profile == dsa::BenchmarkProfile::kPyptoHardV1 &&
              dsa::ValidateStructuredProblemDocument(hard).empty(),
          "hard-v1 fixture should validate");

  auto rejects = [&](dsa::StructuredProblemDocument document, std::string_view feature) {
    const std::vector<std::string> errors = dsa::ValidateStructuredProblemDocument(document);
    Require(ContainsSubstring(errors, feature),
            "pypto_hard_v1 did not reject research feature '" + std::string(feature) + "'");
  };

  dsa::StructuredProblemDocument candidate = hard;
  candidate.problem.buffers.front().live_intervals.push_back({20, 21});
  rejects(candidate, "one conservative live interval");
  candidate = hard;
  candidate.problem.buffers.front().allowed_pools.push_back(candidate.problem.pools.front().id);
  rejects(candidate, "one fixed pool");
  candidate = hard;
  candidate.problem.pools.front().bank_geometry = dsa::BankGeometry{32, 8};
  rejects(candidate, "bank geometry");
  candidate = hard;
  candidate.problem.colocations.push_back({0, 1});
  rejects(candidate, "colocations");
  candidate = hard;
  candidate.problem.temporal_exclusions.push_back({0, 1});
  rejects(candidate, "temporal exclusions");
  candidate = hard;
  candidate.problem.pinned_allocations.push_back({0, candidate.problem.pools.front().id, 0, false});
  rejects(candidate, "pinned allocations");
  candidate = hard;
  candidate.problem.cost_model = dsa::CostModel{};
  rejects(candidate, "cost model");
  candidate = hard;
  candidate.problem.objective = dsa::FitThenMinimizeReuseCostObjective();
  rejects(candidate, "peak-minimization objective");
}

void TestPyptoExportedCorpus() {
  struct CorpusCase {
    const char* filename;
    const char* instance;
    std::size_t buffers;
    std::size_t separations;
    std::size_t pipeline_groups;
    std::size_t reuse_penalties;
    std::uint64_t expected_peak;
  };
  const std::vector<CorpusCase> cases = {
      {"issue_1908_fragmentation_v1.json", "issue_1908_fragmentation", 4, 0, 0, 0, 65'536},
      {"pipeline_stage_separation_v1.json", "pipeline_stage_separation", 2, 1, 1, 0, 32'768},
      {"target_hazard_v1.json", "target_hazard", 3, 1, 0, 0, 8'192},
      {"capacity_gated_pipeline_cost_v1.json", "capacity_gated_pipeline_cost", 3, 0, 1, 2, 245'760},
  };

  dsa::LocalSearchOptions local_options;
  local_options.seed = 7;
  local_options.max_iterations = 200;
  local_options.restarts = 2;
  local_options.stagnation_limit = 50;
  dsa::TvmHillClimbOptions tvm_options;
  tvm_options.seed = 7;
  tvm_options.max_attempts = 200;

  for (const CorpusCase& corpus_case : cases) {
    const std::filesystem::path path = std::filesystem::path(DSA_TEST_SOURCE_DIR) / "benchmarks" /
                                       "pypto" / "unit-tests" / "memory-planning" /
                                       corpus_case.filename;
    const dsa::StructuredProblemDocument document = dsa::ReadStructuredProblemJsonFile(path);
    const dsa::BenchmarkProfile expected_profile = corpus_case.reuse_penalties == 0
                                                       ? dsa::BenchmarkProfile::kPyptoHardV1
                                                       : dsa::BenchmarkProfile::kPyptoResearchV1;
    Require(document.profile == expected_profile, "exported corpus document has the wrong profile");
    Require(document.instance == corpus_case.instance,
            "exported corpus document has the wrong instance name");
    Require(document.problem.buffers.size() == corpus_case.buffers,
            "exported corpus document has the wrong buffer count");
    Require(document.problem.separations.size() == corpus_case.separations,
            "exported corpus document has the wrong separation count");
    Require(
        document.problem.pypto_structure &&
            document.problem.pypto_structure->alias_classes.size() == corpus_case.buffers &&
            document.problem.pypto_structure->pipeline_groups.size() == corpus_case.pipeline_groups,
        "exported corpus document lost normalized PyPTO structure");
    const std::size_t reuse_penalties =
        document.problem.cost_model ? document.problem.cost_model->reuse_penalties.size() : 0;
    Require(reuse_penalties == corpus_case.reuse_penalties,
            "exported corpus document has the wrong reuse-cost edge count");
    Require(document.metadata.at("producer") == "pypto" &&
                document.metadata.at("solver_input") == "pre_memory_reuse" &&
                document.metadata.at("lifetime_ordering") == "pypto_read_before_write" &&
                !document.metadata.at("target").empty(),
            "exported corpus document lost PyPTO provenance");
    const std::vector<std::string> errors = dsa::ValidateStructuredProblemDocument(document);
    Require(errors.empty(), errors.empty() ? "" : errors.front());

    const dsa::DsaResult first_fit = SolveAndValidate(document.problem, dsa::FirstFitSolver());
    Require(first_fit.objective.max_peak == corpus_case.expected_peak,
            "first-fit peak changed for exported corpus document");
    const dsa::DsaResult local_search =
        SolveAndValidate(document.problem, dsa::LocalSearchSolver(local_options));
    Require(local_search.objective.max_peak == corpus_case.expected_peak,
            "local-search peak changed for exported corpus document");
    const dsa::DsaResult tvm_hill_climb =
        SolveAndValidate(document.problem, dsa::TvmHillClimbSolver(tvm_options));
    Require(tvm_hill_climb.objective.max_peak == corpus_case.expected_peak,
            "TVM-style hill-climb peak changed for exported corpus document");
  }
}

void TestEveryPyptoCorpusDocumentIsReplayable() {
  const std::filesystem::path benchmark_root =
      std::filesystem::path(DSA_TEST_SOURCE_DIR) / "benchmarks";
  const std::array<std::filesystem::path, 2> roots = {benchmark_root / "pypto",
                                                      benchmark_root / "pypto-lib"};
  std::vector<std::filesystem::path> paths;
  for (const std::filesystem::path& root : roots) {
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(root)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json") {
        paths.push_back(entry.path());
      }
    }
  }
  std::sort(paths.begin(), paths.end());
  Require(paths.size() >= 5, "PyPTO benchmark corpus unexpectedly lost its seed fixtures");

  std::set<std::string> identities;
  for (const std::filesystem::path& path : paths) {
    const dsa::StructuredProblemDocument document = dsa::ReadStructuredProblemJsonFile(path);
    Require(dsa::IsPyptoProfile(document.profile),
            "PyPTO corpus contains a document with the wrong profile: " + path.string());
    Require(identities.insert(document.instance).second,
            "PyPTO corpus contains duplicate benchmark identity '" + document.instance + "'");
    Require(document.metadata.count("producer") != 0 &&
                document.metadata.at("producer") == "pypto" &&
                document.metadata.count("solver_input") != 0 &&
                document.metadata.at("solver_input") == "pre_memory_reuse" &&
                document.metadata.count("target") != 0 && !document.metadata.at("target").empty(),
            "PyPTO corpus document lost exporter provenance: " + path.string());
    Require(document.problem.pypto_structure.has_value(),
            "PyPTO corpus document lost its compiler provenance: " + path.string());
    const dsa::DsaResult first_fit = dsa::FirstFitSolver().Solve(document.problem);
    Require(first_fit.solution.has_value(),
            "first-fit returned no placement for corpus document: " + path.string());
    Require(first_fit.status == dsa::SolveStatus::kFeasible ||
                first_fit.status == dsa::SolveStatus::kBestEffortNoFit,
            "first-fit returned an invalid status for corpus document: " + path.string());
    dsa::DsaProblem placement_problem = document.problem;
    if (first_fit.status == dsa::SolveStatus::kBestEffortNoFit) {
      for (dsa::Pool& pool : placement_problem.pools) pool.capacity.reset();
    }
    const std::vector<std::string> placement_errors =
        dsa::ValidateSolution(placement_problem, *first_fit.solution);
    Require(placement_errors.empty(),
            "first-fit returned an invalid placement for corpus document: " + path.string());
  }
}

void TestCoreRelaxationProfiles() {
  const dsa::StructuredProblemDocument source = MakeStructuredDocument();
  const std::vector<dsa::StructuredProblemDocument> relaxations = dsa::BuildCoreRelaxations(source);
  Require(relaxations.size() == 2, "one core relaxation was not produced per fixed pool");

  const auto l1 = std::find_if(relaxations.begin(), relaxations.end(), [](const auto& document) {
    return document.metadata.at("source_pool_id") == "3";
  });
  Require(l1 != relaxations.end(), "L1 core relaxation is missing");
  Require(l1->profile == dsa::BenchmarkProfile::kPyptoCoreRelaxation &&
              l1->relaxed_from == source.instance,
          "core-relaxation provenance is missing");
  Require(l1->problem.buffers.size() == 3,
          "multi-interval buffer was not split into independent standard rows");
  Require(Contains(l1->relaxed_features, "multi_interval_identity") &&
              Contains(l1->relaxed_features, "reserved_ranges") &&
              Contains(l1->relaxed_features, "separations") &&
              Contains(l1->relaxed_features, "pypto_structure"),
          "core relaxation did not disclose removed structure");
  Require(dsa::ValidateStructuredProblemDocument(*l1).empty(),
          "generated core relaxation is not standard-profile compatible");
  std::ostringstream serialized;
  dsa::WriteStructuredProblemJson(serialized, *l1);
  std::istringstream serialized_input(serialized.str());
  const dsa::StructuredProblemDocument reparsed = dsa::ReadStructuredProblemJson(serialized_input);
  Require(reparsed.profile == dsa::BenchmarkProfile::kPyptoCoreRelaxation &&
              reparsed.relaxed_features == l1->relaxed_features,
          "core-relaxation provenance did not round-trip");
  const dsa::DsaResult result = SolveAndValidate(l1->problem, dsa::FirstFitSolver());
  Require(result.objective.max_peak > 0, "core relaxation did not execute");

  dsa::StructuredProblemDocument unsupported = source;
  unsupported.problem.temporal_exclusions.push_back({0, 1});
  bool rejected = false;
  try {
    static_cast<void>(dsa::BuildCoreRelaxations(unsupported));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  Require(rejected, "unsound temporal-exclusion projection was silently accepted");

  unsupported = source;
  unsupported.problem.colocations.push_back({0, 1});
  rejected = false;
  try {
    static_cast<void>(dsa::BuildCoreRelaxations(unsupported));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  Require(rejected, "unsound colocation projection was silently accepted");

  unsupported = source;
  unsupported.problem.buffers[0].live_intervals = {{0, 3}, {2, 5}};
  rejected = false;
  try {
    static_cast<void>(dsa::BuildCoreRelaxations(unsupported));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  Require(rejected, "unsound overlapping-interval split was silently accepted");
}

void TestSolverCapabilityMatching() {
  const dsa::StructuredProblemDocument document = MakeStructuredDocument();
  const dsa::FirstFitSolver first_fit;
  const dsa::SolverCompatibility first_fit_match =
      dsa::CheckSolverCompatibility(document.problem, first_fit.Capabilities());
  Require(first_fit_match.StructurallyCompatible(),
          "first-fit should support the fixture's hard constraints");
  Require(!first_fit_match.ObjectiveCompatible() &&
              Contains(first_fit_match.unsupported_objectives, "metric:reuse_cost"),
          "first-fit did not disclose its unsupported reuse objective");
  const dsa::DsaResult baseline = first_fit.Solve(document.problem);
  Require(baseline.status == dsa::SolveStatus::kFeasible && !baseline.diagnostics.empty(),
          "first-fit did not provide a disclosed structural baseline");

  const dsa::LocalSearchSolver local_search;
  const dsa::SolverCompatibility local_match =
      dsa::CheckSolverCompatibility(document.problem, local_search.Capabilities());
  Require(local_match.Compatible(), "local search should match the structured fixture");
  Require(local_search.Solve(document.problem).status == dsa::SolveStatus::kFeasible,
          "compatible local search did not run");

  const dsa::TvmHillClimbSolver tvm_hill_climb;
  const dsa::SolverCompatibility tvm_match =
      dsa::CheckSolverCompatibility(document.problem, tvm_hill_climb.Capabilities());
  Require(tvm_match.Compatible(), "TVM-style hill climb should match the structured fixture");
  Require(tvm_hill_climb.Solve(document.problem).status == dsa::SolveStatus::kFeasible,
          "compatible TVM-style hill climb did not run");

  const dsa::PyptoStructuredSearchSolver pypto_structured_search;
  const dsa::SolverCompatibility pypto_match =
      dsa::CheckSolverCompatibility(document.problem, pypto_structured_search.Capabilities());
  Require(pypto_match.Compatible(), "PyPTO structured search should match the fixture");
  Require(pypto_structured_search.Solve(document.problem).status == dsa::SolveStatus::kFeasible,
          "compatible PyPTO structured search did not run");

  const dsa::XlaHeapSolver xla_heap;
  const dsa::SolverCompatibility xla_match =
      dsa::CheckSolverCompatibility(document.problem, xla_heap.Capabilities());
  Require(!xla_match.StructurallyCompatible() &&
              Contains(xla_match.unsupported_features, "multi_interval"),
          "XLA heap silently accepted PyPTO-only structure");
  Require(xla_heap.Solve(document.problem).status == dsa::SolveStatus::kUnsupported,
          "XLA heap did not reject a structured PyPTO problem");

  dsa::DsaProblem bank_objective = document.problem;
  bank_objective.objective.terms = {dsa::ObjectiveMetric::kBankCost};
  const dsa::SolverCompatibility bank_match =
      dsa::CheckSolverCompatibility(bank_objective, local_search.Capabilities());
  Require(!bank_match.ObjectiveCompatible() &&
              Contains(bank_match.unsupported_objectives, "metric:bank_cost"),
          "unimplemented bank objective was silently advertised");
}

void TestCypressRelaxationBaseline() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{0, 2}}, 6),
      MakeBuffer(1, {{0, 4}}, 4),
      MakeBuffer(2, {{2, 4}}, 3),
  };
  problem.pools.front().capacity = 10;

  const dsa::CypressRelaxationSolver solver;
  const dsa::DsaResult result = SolveAndValidate(problem, solver);
  Require(OffsetOf(result, 0) == 4 && OffsetOf(result, 1) == 0 && OffsetOf(result, 2) == 4,
          "Cypress baseline did not reproduce the frozen Knight placement");
  Require(result.objective.max_peak == 10,
          "Cypress baseline did not fit the capacity after one relaxation");
  Require(result.solver_metrics.at("relaxed_edges") == 1 &&
              result.solver_metrics.at("actual_alias_pairs") == 1 &&
              result.solver_metrics.at("packing_attempts") == 2,
          "Cypress baseline did not report its relaxation accounting");

  problem.pools.front().capacity = 13;
  const dsa::DsaResult separated = SolveAndValidate(problem, solver);
  Require(separated.objective.max_peak == 13 && separated.solver_metrics.at("relaxed_edges") == 0 &&
              separated.solver_metrics.at("actual_alias_pairs") == 0,
          "Cypress baseline relaxed a complete graph that already fit");

  problem.pools.front().capacity.reset();
  const dsa::DsaResult no_capacity = solver.Solve(problem);
  Require(no_capacity.status == dsa::SolveStatus::kUnsupported &&
              ContainsSubstring(no_capacity.diagnostics, "requires a fixed capacity"),
          "Cypress baseline silently ran without its defining capacity bound");

  dsa::DsaProblem mandatory_no_fit;
  mandatory_no_fit.buffers = {
      MakeBuffer(0, {{0, 2}}, 6),
      MakeBuffer(1, {{0, 2}}, 6),
  };
  mandatory_no_fit.pools.front().capacity = 10;
  const dsa::DsaResult no_fit = solver.Solve(mandatory_no_fit);
  Require(no_fit.status == dsa::SolveStatus::kBestEffortNoFit && no_fit.solution.has_value() &&
              no_fit.solver_metrics.at("relaxed_edges") == 0,
          "Cypress heuristic failure was incorrectly reported as proven infeasibility");
}

void TestInvalidProblemIsReported() {
  dsa::DsaProblem problem;
  problem.buffers = {MakeBuffer(0, {{4, 4}}, 32)};
  const dsa::DsaResult result = dsa::FirstFitSolver().Solve(problem);
  Require(result.status == dsa::SolveStatus::kInvalidProblem,
          "empty half-open lifetime was not rejected");
  Require(!result.diagnostics.empty(), "invalid problem has no diagnostic");
}

void TestXlaSpatialBestFitConformance() {
  const dsa::XlaHeapSolver solver;

  dsa::DsaProblem decreasing_size;
  decreasing_size.buffers = {
      MakeBuffer(0, {{0, 4}}, 10),
      MakeBuffer(1, {{1, 5}}, 30),
      MakeBuffer(2, {{2, 6}}, 20),
      MakeBuffer(3, {{3, 7}}, 40),
  };
  dsa::DsaResult result = SolveAndValidate(decreasing_size, solver);
  Require(result.objective.max_peak == 100, "XLA decreasing-size peak changed");
  Require(OffsetOf(result, 0) == 90 && OffsetOf(result, 1) == 40 && OffsetOf(result, 2) == 70 &&
              OffsetOf(result, 3) == 0,
          "XLA decreasing-size placement differs from OpenXLA conformance case");

  dsa::DsaProblem best_fit;
  best_fit.buffers = {
      MakeBuffer(0, {{0, 3}}, 10), MakeBuffer(1, {{1, 6}}, 20), MakeBuffer(2, {{2, 7}}, 40),
      MakeBuffer(3, {{4, 8}}, 30), MakeBuffer(4, {{5, 9}}, 50),
  };
  result = SolveAndValidate(best_fit, solver);
  Require(result.objective.max_peak == 140, "XLA best-fit peak changed");
  Require(OffsetOf(result, 0) == 90 && OffsetOf(result, 1) == 120 && OffsetOf(result, 2) == 50 &&
              OffsetOf(result, 3) == 90 && OffsetOf(result, 4) == 0,
          "XLA best-fit placement differs from OpenXLA conformance case");

  dsa::DsaProblem aligned;
  aligned.buffers = {
      MakeBuffer(0, {{0, 3}}, 10),
      MakeBuffer(1, {{1, 5}}, 20),
      MakeBuffer(2, {{2, 6}}, 50),
      MakeBuffer(3, {{4, 7}}, 40),
  };
  for (dsa::Buffer& buffer : aligned.buffers) buffer.alignment = 20;
  result = SolveAndValidate(aligned, solver);
  Require(result.objective.max_peak == 120, "XLA aligned peak changed");
  Require(OffsetOf(result, 0) == 60 && OffsetOf(result, 1) == 100 && OffsetOf(result, 2) == 0 &&
              OffsetOf(result, 3) == 60,
          "XLA aligned placement differs from OpenXLA conformance case");

  dsa::DsaProblem colocated_alignment;
  colocated_alignment.buffers = {
      MakeBuffer(0, {{0, 2}}, 10),
      MakeBuffer(1, {{2, 4}}, 10),
      MakeBuffer(2, {{0, 4}}, 1),
  };
  colocated_alignment.buffers[0].alignment = 6;
  colocated_alignment.buffers[1].alignment = 8;
  colocated_alignment.colocations.push_back({0, 1});
  result = SolveAndValidate(colocated_alignment, solver);
  Require(OffsetOf(result, 0) == OffsetOf(result, 1),
          "XLA colocation members do not share an offset");
  Require(OffsetOf(result, 0) % 24 == 0,
          "colocation class did not use the least common multiple alignment");

  dsa::DsaProblem overflowing_alignment = colocated_alignment;
  overflowing_alignment.buffers[0].alignment = std::numeric_limits<std::uint64_t>::max();
  overflowing_alignment.buffers[1].alignment = 2;
  Require(solver.Solve(overflowing_alignment).status == dsa::SolveStatus::kInvalidProblem,
          "overflowing colocation alignment was not rejected");
}

void TestPyptoStructuredSearchUsesStructureAndObjective() {
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{2, 9}}, 5), MakeBuffer(1, {{4, 8}}, 3), MakeBuffer(2, {{7, 10}}, 3),
      MakeBuffer(3, {{8, 9}}, 2), MakeBuffer(4, {{0, 6}}, 6),
  };
  problem.pools.front().capacity = 14;
  dsa::CostModel cost_model;
  cost_model.reuse_penalties.push_back({3, 4, 100, dsa::ReusePenaltyReason::kCrossPipe});
  problem.cost_model = std::move(cost_model);
  problem.objective = dsa::FitThenMinimizeReuseCostObjective();
  dsa::PyptoStructure structure;
  for (const dsa::Buffer& buffer : problem.buffers) {
    structure.alias_classes.push_back({buffer.id, {buffer.name}});
  }
  problem.pypto_structure = std::move(structure);

  dsa::PyptoStructuredSearchOptions options;
  options.seed = 11;
  options.max_iterations = 2'000;
  options.restarts = 4;
  options.stagnation_limit = 100;
  const dsa::PyptoStructuredSearchSolver solver(options);
  const dsa::DsaResult first = SolveAndValidate(problem, solver);
  const dsa::DsaResult second = SolveAndValidate(problem, solver);
  Require(first.objective.max_peak <= 14 && first.objective.reuse_cost == 0,
          "PyPTO structured search did not honor the fit/reuse-cost objective");
  Require(SolutionOrThrow(first).placements == SolutionOrThrow(second).placements,
          "PyPTO structured search is not deterministic for a fixed seed");
  Require(std::any_of(first.diagnostics.begin(), first.diagnostics.end(),
                      [](const std::string& diagnostic) {
                        return diagnostic.find("PyPTO structured search evaluated") !=
                               std::string::npos;
                      }),
          "PyPTO structured search did not report its evaluation budget");

  dsa::DsaProblem standard = problem;
  standard.pypto_structure.reset();
  Require(solver.Solve(standard).status == dsa::SolveStatus::kUnsupported,
          "PyPTO structured search silently accepted an unstructured problem");
}

void TestArchitectureBinding() {
  const std::filesystem::path source_dir = DSA_TEST_SOURCE_DIR;
  const dsa::StructuredProblemDocument program =
      dsa::ReadStructuredProblemJsonFile(source_dir / "tests/data/pypto_unbound_program_v1.json");
  const dsa::ArchitectureSpec ascend_910b =
      dsa::ReadArchitectureSpecJsonFile(source_dir / "benchmarks/architectures/ascend910b-v1.json");
  const dsa::ArchitectureSpec ascend_950 =
      dsa::ReadArchitectureSpecJsonFile(source_dir / "benchmarks/architectures/ascend950-v1.json");

  Require(dsa::ValidateUnboundProgram(program).empty(),
          "portable test program does not satisfy the unbound contract");
  Require(dsa::ValidateArchitectureSpec(ascend_910b).empty(),
          "Ascend 910B architecture specification is invalid");
  Require(dsa::ValidateArchitectureSpec(ascend_950).empty(),
          "Ascend 950 architecture specification is invalid");

  const std::string program_fingerprint = dsa::FingerprintUnboundProgram(program);
  const std::string ascend_910b_fingerprint = dsa::FingerprintArchitectureSpec(ascend_910b);
  const std::string ascend_950_fingerprint = dsa::FingerprintArchitectureSpec(ascend_950);
  Require(program_fingerprint.size() == 16, "program fingerprint is not FNV-1a-64 hex");
  Require(ascend_910b_fingerprint.size() == 16, "architecture fingerprint is not FNV-1a-64 hex");
  Require(ascend_910b_fingerprint != ascend_950_fingerprint,
          "different architecture specifications have the same fingerprint");

  dsa::StructuredProblemDocument renumbered = program;
  renumbered.problem.pools[0].id = 42;
  renumbered.problem.pools[1].id = 7;
  renumbered.problem.buffers[0].allowed_pools = {42};
  renumbered.problem.buffers[1].allowed_pools = {42};
  renumbered.problem.buffers[2].allowed_pools = {7};
  std::reverse(renumbered.problem.pools.begin(), renumbered.problem.pools.end());
  Require(dsa::FingerprintUnboundProgram(renumbered) == program_fingerprint,
          "serialization-local pool IDs changed the program fingerprint");

  dsa::StructuredProblemDocument pipeline_program = program;
  pipeline_program.problem.pypto_structure->pipeline_groups.push_back(
      {3, 1, 32768, 2, 2, {{0, 0, 0}, {1, 1, 1}}});
  dsa::StructuredProblemDocument renumbered_pipeline_program = renumbered;
  renumbered_pipeline_program.problem.pypto_structure->pipeline_groups.push_back(
      {3, 42, 32768, 2, 2, {{0, 0, 0}, {1, 1, 1}}});
  Require(dsa::FingerprintUnboundProgram(pipeline_program) ==
              dsa::FingerprintUnboundProgram(renumbered_pipeline_program),
          "pipeline-group pool IDs changed the program fingerprint");

  dsa::ArchitectureSpec reordered_architecture = ascend_910b;
  std::reverse(reordered_architecture.supported_lowering_abis.begin(),
               reordered_architecture.supported_lowering_abis.end());
  std::reverse(reordered_architecture.memory_spaces.begin(),
               reordered_architecture.memory_spaces.end());
  Require(dsa::FingerprintArchitectureSpec(reordered_architecture) == ascend_910b_fingerprint,
          "architecture array ordering changed its fingerprint");

  const dsa::StructuredProblemDocument bound_910b = dsa::BindArchitecture(program, ascend_910b);
  const dsa::StructuredProblemDocument bound_950 = dsa::BindArchitecture(program, ascend_950);
  const dsa::Pool* vector_910b = bound_910b.problem.FindPool(1);
  const dsa::Pool* accumulator_910b = bound_910b.problem.FindPool(5);
  const dsa::Pool* vector_950 = bound_950.problem.FindPool(1);
  const dsa::Pool* accumulator_950 = bound_950.problem.FindPool(5);
  Require(vector_910b != nullptr && vector_910b->capacity == 188416,
          "Ascend 910B UB capacity was not bound");
  Require(accumulator_910b != nullptr && accumulator_910b->capacity == 131072,
          "Ascend 910B L0C capacity was not bound");
  Require(vector_950 != nullptr && vector_950->capacity == 245760,
          "Ascend 950 UB capacity was not bound");
  Require(accumulator_950 != nullptr && accumulator_950->capacity == 262144,
          "Ascend 950 L0C capacity was not bound");
  Require(bound_910b.problem.buffers[0].alignment == 32 &&
              bound_910b.problem.buffers[1].alignment == 64 &&
              bound_910b.problem.buffers[2].alignment == 32,
          "architecture alignment was not combined with program alignment");
  Require(bound_910b.metadata.at("program_fingerprint_fnv1a64") == program_fingerprint &&
              bound_950.metadata.at("program_fingerprint_fnv1a64") == program_fingerprint,
          "paired bindings did not retain one program identity");
  Require(bound_910b.metadata.at("architecture_id") == "Ascend910B" &&
              bound_950.metadata.at("architecture_id") == "Ascend950",
          "bound documents lost their architecture identity");
  Require(bound_910b.instance == program.instance + "@Ascend910B",
          "bound instance name does not identify the architecture");
  Require(dsa::ValidateStructuredProblemDocument(bound_910b).empty(),
          "Ascend 910B binding is not a valid structured problem");
  Require(dsa::ValidateStructuredProblemDocument(bound_950).empty(),
          "Ascend 950 binding is not a valid structured problem");
  SolveAndValidate(bound_910b.problem, dsa::FirstFitSolver());
  SolveAndValidate(bound_950.problem, dsa::FirstFitSolver());

  Require(!dsa::ValidateUnboundProgram(bound_910b).empty(),
          "bound document was accepted as an unbound program");
  bool rejected_bound_input = false;
  try {
    static_cast<void>(dsa::BindArchitecture(bound_910b, ascend_910b));
  } catch (const std::invalid_argument&) {
    rejected_bound_input = true;
  }
  Require(rejected_bound_input, "binder silently rebound an already-bound document");

  dsa::StructuredProblemDocument a2a3_program = program;
  a2a3_program.metadata["lowering_abi"] = "pypto-a2a3-v1";
  static_cast<void>(dsa::BindArchitecture(a2a3_program, ascend_910b));
  bool rejected_incompatible_abi = false;
  try {
    static_cast<void>(dsa::BindArchitecture(a2a3_program, ascend_950));
  } catch (const std::invalid_argument&) {
    rejected_incompatible_abi = true;
  }
  Require(rejected_incompatible_abi,
          "binder silently accepted an architecture-incompatible lowering ABI");

  dsa::ArchitectureSpec incomplete_architecture = ascend_910b;
  incomplete_architecture.memory_spaces.erase(
      std::remove_if(
          incomplete_architecture.memory_spaces.begin(),
          incomplete_architecture.memory_spaces.end(),
          [](const dsa::ArchitectureMemorySpace& space) { return space.logical_space == "Acc"; }),
      incomplete_architecture.memory_spaces.end());
  bool rejected_missing_space = false;
  try {
    static_cast<void>(dsa::BindArchitecture(program, incomplete_architecture));
  } catch (const std::invalid_argument&) {
    rejected_missing_space = true;
  }
  Require(rejected_missing_space,
          "binder silently accepted an architecture without a required logical space");

  dsa::ArchitectureSpec invalid_architecture = ascend_910b;
  invalid_architecture.memory_spaces.front().usable_capacity = 0;
  Require(!dsa::ValidateArchitectureSpec(invalid_architecture).empty(),
          "architecture validator accepted a zero usable capacity");
}

void TestFlexiblePoolAssignmentIsExplicitlyUnsupported() {
  dsa::DsaProblem problem;
  problem.pools = {{0, "left", 64, {}, std::nullopt}, {1, "right", 64, {}, std::nullopt}};
  problem.buffers = {MakeBuffer(0, {{0, 4}}, 32)};
  problem.buffers.front().allowed_pools = {0, 1};
  const dsa::FirstFitSolver solver;
  Require(!solver.Capabilities().flexible_pool_assignment,
          "first-fit incorrectly advertises flexible pool assignment");
  const dsa::DsaResult result = solver.Solve(problem);
  Require(result.status == dsa::SolveStatus::kUnsupported,
          "flexible pool assignment was silently accepted");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string, void (*)()>> tests = {
      {"MiniMalloc CSV", TestMiniMallocCsv},
      {"first-fit #1908", TestFirstFitSubdividesFreedRegion},
      {"multi-interval", TestMultiIntervalLiveness},
      {"hard constraints", TestHardConstraintsAndPinnedRanges},
      {"colocation", TestColocation},
      {"temporal exclusion", TestTemporalExclusionOverridesConservativeHulls},
      {"fixed pools", TestFixedPoolsAreIndependent},
      {"reuse cost", TestFitCostObjectiveAvoidsExpensiveReuse},
      {"DSA-RP constructive baselines", TestDsaRpConstructiveBaselines},
      {"DSA-RP exact solvers", TestDsaRpExactSolvers},
      {"DSA-RP promoted-set local search", TestDsaRpPromotedSetLocalSearch},
      {"sparse reference", TestSparseReferenceReducesPhysicalReuse},
      {"structured solution replay", TestStructuredSolutionRoundTripAndMismatchRejection},
      {"pipeline intent relaxation", TestPipelineIntentRelaxationPreservesNonPipelineReasons},
      {"local search", TestLocalSearchClosesOrderingGap},
      {"local search budget", TestLocalSearchUsesGlobalEvaluationBudget},
      {"TVM hill climb", TestTvmHillClimbClosesOrderingGap},
      {"structured JSON", TestStructuredJsonRoundTripAndProfiles},
      {"PyPTO hard-v1 profile", TestPyptoHardV1RejectsResearchFeatures},
      {"PyPTO exported corpus", TestPyptoExportedCorpus},
      {"PyPTO recursive corpus", TestEveryPyptoCorpusDocumentIsReplayable},
      {"core relaxation", TestCoreRelaxationProfiles},
      {"solver capabilities", TestSolverCapabilityMatching},
      {"Cypress relaxation", TestCypressRelaxationBaseline},
      {"XLA spatial best fit", TestXlaSpatialBestFitConformance},
      {"PyPTO structured search", TestPyptoStructuredSearchUsesStructureAndObjective},
      {"architecture binding", TestArchitectureBinding},
      {"invalid problem", TestInvalidProblemIsReported},
      {"flexible pools", TestFlexiblePoolAssignmentIsExplicitlyUnsupported},
  };

  std::size_t failed = 0;
  for (const auto& [name, test] : tests) {
    try {
      test();
      std::cout << "PASS: " << name << '\n';
    } catch (const std::exception& error) {
      ++failed;
      std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
    }
  }
  if (failed != 0) {
    std::cerr << failed << " test(s) failed\n";
    return 1;
  }
  std::cout << tests.size() << " test(s) passed\n";
  return 0;
}

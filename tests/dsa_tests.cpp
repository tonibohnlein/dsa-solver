#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dsa/first_fit_solver.h"
#include "dsa/local_search_solver.h"
#include "dsa/minimalloc_csv.h"
#include "dsa/validator.h"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
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

dsa::DsaResult SolveAndValidate(const dsa::DsaProblem& problem, const dsa::DsaSolver& solver) {
  dsa::DsaResult result = solver.Solve(problem);
  Require(result.status == dsa::SolveStatus::kFeasible, "solver did not find a feasible solution");
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

  dsa::LocalSearchOptions options;
  options.seed = 11;
  options.max_iterations = 2'000;
  options.restarts = 4;
  options.stagnation_limit = 100;
  options.objective = dsa::LocalSearchObjective::kFitThenMinimizeReuseCost;
  const dsa::DsaResult result = SolveAndValidate(problem, dsa::LocalSearchSolver(options));
  Require(result.objective.max_peak <= 14 && result.objective.reuse_cost == 0,
          "fit-cost local search did not avoid the expensive reuse edge");
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

void TestInvalidProblemIsReported() {
  dsa::DsaProblem problem;
  problem.buffers = {MakeBuffer(0, {{4, 4}}, 32)};
  const dsa::DsaResult result = dsa::FirstFitSolver().Solve(problem);
  Require(result.status == dsa::SolveStatus::kInvalidProblem,
          "empty half-open lifetime was not rejected");
  Require(!result.diagnostics.empty(), "invalid problem has no diagnostic");
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
      {"local search", TestLocalSearchClosesOrderingGap},
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

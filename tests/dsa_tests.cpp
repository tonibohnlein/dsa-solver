#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
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
#include "dsa/structured_problem.h"
#include "dsa/tvm_hill_climb_solver.h"
#include "dsa/validator.h"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

bool Contains(const std::vector<std::string>& values, std::string_view expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
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

void TestPyptoWholeSlotReuseDoesNotSubdivideFreedRegion() {
  constexpr std::uint64_t kKiB = 1024;
  dsa::DsaProblem problem;
  problem.buffers = {
      MakeBuffer(0, {{0, 3}}, 64 * kKiB),
      MakeBuffer(1, {{3, 6}}, 32 * kKiB),
      MakeBuffer(2, {{3, 6}}, 32 * kKiB),
  };
  problem.pools.front().capacity = 96 * kKiB;
  dsa::PyptoStructure structure;
  structure.whole_slot_reuse = true;
  structure.alias_classes = {{0, {"b0"}}, {1, {"b1"}}, {2, {"b2"}}};
  problem.pypto_structure = std::move(structure);

  const dsa::DsaResult result = SolveAndValidate(problem, dsa::FirstFitSolver());
  Require(result.objective.max_peak == 96 * kKiB,
          "PyPTO whole-slot reuse partially subdivided a freed producer slot");
  Require((OffsetOf(result, 1) == 0 && OffsetOf(result, 2) == 64 * kKiB) ||
              (OffsetOf(result, 2) == 0 && OffsetOf(result, 1) == 64 * kKiB),
          "PyPTO consumers did not reuse one whole slot plus one disjoint slot");

  dsa::DsaSolution partial;
  partial.placements = {{0, {dsa::kDefaultPool, 0}},
                        {1, {dsa::kDefaultPool, 0}},
                        {2, {dsa::kDefaultPool, 32 * kKiB}}};
  Require(!dsa::ValidateSolution(problem, partial).empty(),
          "PyPTO validator accepted a partial overlap at different base addresses");
}

void TestStandardFreedRegionSubdivisionCorpus() {
  const std::filesystem::path path = std::filesystem::path(DSA_TEST_SOURCE_DIR) / "benchmarks" /
                                     "standard" / "freed_region_subdivision_v1.json";
  const dsa::StructuredProblemDocument document = dsa::ReadStructuredProblemJsonFile(path);
  Require(document.profile == dsa::BenchmarkProfile::kStandardDsa,
          "subdivision corpus case must use the standard profile");
  Require(document.problem.buffers.size() == 3, "subdivision corpus buffer count changed");

  const dsa::DsaResult result = SolveAndValidate(document.problem, dsa::FirstFitSolver());
  Require(result.objective.max_peak == 100, "A100 -> B60+C40 did not fit at height 100");
  Require(OffsetOf(result, 0) == 0 && OffsetOf(result, 1) == 0 && OffsetOf(result, 2) == 60,
          "A100 -> B60+C40 did not subdivide the freed address interval");
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
  document.profile = dsa::BenchmarkProfile::kPyptoStructured;
  document.instance = "pypto_fixture";
  document.metadata = {{"kernel", "fixture"}, {"target", "ascend910b"}};
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
  cost_model.reuse_penalties.push_back({0, 1, 25, dsa::ReusePenaltyReason::kCrossPipe});
  document.problem.cost_model = std::move(cost_model);
  dsa::PyptoStructure structure;
  structure.whole_slot_reuse = true;
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
  Require(parsed.profile == dsa::BenchmarkProfile::kPyptoStructured,
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
  Require(parsed.problem.pypto_structure && parsed.problem.pypto_structure->whole_slot_reuse &&
              parsed.problem.pypto_structure->alias_classes.front().members.size() == 2 &&
              parsed.problem.pypto_structure->pipeline_groups.front().effective_depth == 2,
          "normalized PyPTO structure did not round-trip");
  Require(
      parsed.problem.cost_model && parsed.problem.cost_model->reuse_penalties.front().cost == 25,
      "structured cost model did not round-trip");
  Require(parsed.problem.objective.terms == dsa::FitThenMinimizeReuseCostObjective().terms,
          "structured objective did not round-trip");

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
      {"chain_read_before_write_v1.json", "read_before_write_chain", 3, 0, 0, 0, 16'384},
      {"issue_1908_fragmentation_v1.json", "issue_1908_fragmentation", 4, 0, 0, 0, 98'304},
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
    const std::filesystem::path path =
        std::filesystem::path(DSA_TEST_SOURCE_DIR) / "benchmarks" / "pypto" / corpus_case.filename;
    const dsa::StructuredProblemDocument document = dsa::ReadStructuredProblemJsonFile(path);
    Require(document.profile == dsa::BenchmarkProfile::kPyptoStructured,
            "exported corpus document has the wrong profile");
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

  dsa::DsaProblem bank_objective = document.problem;
  bank_objective.objective.terms = {dsa::ObjectiveMetric::kBankCost};
  const dsa::SolverCompatibility bank_match =
      dsa::CheckSolverCompatibility(bank_objective, local_search.Capabilities());
  Require(!bank_match.ObjectiveCompatible() &&
              Contains(bank_match.unsupported_objectives, "metric:bank_cost"),
          "unimplemented bank objective was silently advertised");
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
      {"PyPTO whole-slot reuse", TestPyptoWholeSlotReuseDoesNotSubdivideFreedRegion},
      {"standard subdivision corpus", TestStandardFreedRegionSubdivisionCorpus},
      {"multi-interval", TestMultiIntervalLiveness},
      {"hard constraints", TestHardConstraintsAndPinnedRanges},
      {"colocation", TestColocation},
      {"temporal exclusion", TestTemporalExclusionOverridesConservativeHulls},
      {"fixed pools", TestFixedPoolsAreIndependent},
      {"reuse cost", TestFitCostObjectiveAvoidsExpensiveReuse},
      {"local search", TestLocalSearchClosesOrderingGap},
      {"local search budget", TestLocalSearchUsesGlobalEvaluationBudget},
      {"TVM hill climb", TestTvmHillClimbClosesOrderingGap},
      {"structured JSON", TestStructuredJsonRoundTripAndProfiles},
      {"PyPTO exported corpus", TestPyptoExportedCorpus},
      {"core relaxation", TestCoreRelaxationProfiles},
      {"solver capabilities", TestSolverCapabilityMatching},
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

// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "dsa/first_fit_solver.h"
#include "dsa/local_search_solver.h"
#include "dsa/minimalloc_csv.h"
#include "dsa/pypto_structured_search_solver.h"
#include "dsa/structured_problem.h"
#include "dsa/tvm_hill_climb_solver.h"
#include "dsa/validator.h"
#include "dsa/xla_heap_solver.h"

#ifdef DSA_HAVE_MINIMALLOC
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "minimalloc.h"
#include "solver.h"
#endif

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

constexpr std::string_view kFirstFit = "first_fit";
constexpr std::string_view kXlaHeap = "xla_heap";
constexpr std::string_view kTvmHillClimb = "tvm_hill_climb";
constexpr std::string_view kLocalSearch = "local_search";
constexpr std::string_view kPyptoStructuredSearch = "pypto_structured_search";
constexpr std::string_view kMiniMalloc = "minimalloc_exact";

struct Options {
  std::vector<fs::path> standard_roots;
  std::vector<fs::path> pypto_roots;
  fs::path output_dir = "benchmark-results";
  std::optional<std::uint64_t> standard_capacity;
  std::vector<std::uint64_t> seeds{0, 1, 2};
  std::size_t iterations = 2'000;
  std::size_t restarts = 4;
  std::size_t stagnation_limit = 250;
  std::uint64_t minimalloc_timeout_ms = 60'000;
  bool run_minimalloc = true;
  bool build_core_relaxations = true;
  std::string run_label = "local";
};

struct Instance {
  dsa::StructuredProblemDocument document;
  std::string source;
};

struct FeatureStats {
  std::string instance;
  std::string profile;
  std::size_t buffers = 0;
  std::size_t pools = 0;
  std::size_t aligned_buffers = 0;
  std::size_t multi_interval_buffers = 0;
  std::size_t flexible_pool_buffers = 0;
  std::size_t reserved_ranges = 0;
  std::size_t bank_geometries = 0;
  std::size_t colocations = 0;
  std::size_t separations = 0;
  std::size_t pipeline_separations = 0;
  std::size_t target_hazard_separations = 0;
  std::size_t semantic_no_alias_separations = 0;
  std::size_t temporal_exclusions = 0;
  std::size_t pinned_allocations = 0;
  std::size_t reuse_penalties = 0;
  std::size_t nontrivial_alias_classes = 0;
  std::size_t pipeline_groups = 0;
  std::size_t depth_shed_pipeline_groups = 0;
  bool whole_slot_reuse = false;
};

struct RunRecord {
  std::string instance;
  std::string profile;
  std::string source;
  std::string method;
  std::string comparison_scope;
  std::string status;
  std::optional<std::string> relaxed_from;
  std::optional<std::uint64_t> seed;
  std::optional<std::uint64_t> capacity;
  std::optional<std::uint64_t> peak;
  std::optional<std::uint64_t> total_peak;
  std::optional<std::uint64_t> capacity_overflow;
  std::optional<std::uint64_t> reuse_cost;
  std::optional<std::uint64_t> bank_cost;
  std::optional<std::uint64_t> backtracks;
  std::uint64_t runtime_us = 0;
  std::size_t buffers = 0;
  bool placement_valid = false;
  bool solution_valid = false;
  bool structurally_compatible = true;
  bool objective_compatible = true;
  bool certified_optimal = false;
  std::string target;
  std::vector<std::string> features;
  std::vector<std::string> relaxed_features;
  std::vector<std::string> diagnostics;
  std::vector<std::uint64_t> objective_score;
  std::map<std::string, std::string> metadata;
};

struct Summary {
  std::string instance;
  std::string profile;
  std::string source;
  std::string corpus_family;
  std::string corpus_source_path;
  std::string method;
  std::string comparison_scope;
  std::string status;
  std::optional<std::string> relaxed_from;
  std::optional<std::uint64_t> capacity;
  std::optional<RunRecord> best;
  std::size_t runs = 0;
  std::size_t feasible_runs = 0;
  std::size_t placement_valid_runs = 0;
  std::size_t valid_runs = 0;
  std::uint64_t median_runtime_us = 0;
  bool certified_optimal = false;
  std::string target;
  std::vector<std::string> features;
};

[[noreturn]] void UsageError(const std::string& message) {
  throw std::invalid_argument(message + "\nRun dsa-suite --help for usage information.");
}

std::uint64_t ParseUnsigned(std::string_view text, const std::string& option) {
  if (text.empty() || text.front() == '-') UsageError(option + " requires a non-negative value");
  std::uint64_t parsed = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (error != std::errc{} || end != text.data() + text.size()) {
    UsageError(option + " has an invalid integer value");
  }
  return parsed;
}

std::size_t ParseSize(std::string_view text, const std::string& option) {
  const std::uint64_t parsed = ParseUnsigned(text, option);
  if (parsed > std::numeric_limits<std::size_t>::max()) {
    UsageError(option + " exceeds the platform size range");
  }
  return static_cast<std::size_t>(parsed);
}

std::vector<std::uint64_t> ParseSeeds(std::string_view text) {
  std::vector<std::uint64_t> seeds;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    const std::size_t comma = text.find(',', begin);
    const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
    if (end == begin) UsageError("--seeds contains an empty value");
    seeds.push_back(ParseUnsigned(text.substr(begin, end - begin), "--seeds"));
    if (comma == std::string_view::npos) break;
    begin = comma + 1;
  }
  std::sort(seeds.begin(), seeds.end());
  seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
  if (seeds.empty()) UsageError("--seeds requires at least one value");
  return seeds;
}

void PrintHelp() {
  std::cout
      << "Usage: dsa-suite (--standard PATH | --pypto PATH)... [options]\n\n"
      << "Inputs (repeatable; PATH may be a file or directory):\n"
      << "  --standard PATH                 MiniMalloc CSV or standard JSON corpus\n"
      << "  --pypto PATH                    PyPTO structured JSON corpus\n\n"
      << "Output and budgets:\n"
      << "  --output-dir DIR                Write results, summary, features, and report files\n"
      << "  --run-label TEXT                Stable label stored in the report\n"
      << "  --standard-capacity BYTES       Capacity/upper bound for standard CSV inputs\n"
      << "  --seeds N[,N...]                Search seeds (default: 0,1,2)\n"
      << "  --iterations N                  Local/TVM search budget (default: 2000)\n"
      << "  --restarts N                    Local-search restarts (default: 4)\n"
      << "  --stagnation N                  Local-search stagnation limit (default: 250)\n"
      << "  --minimalloc-timeout-ms N       Exact-solver budget per instance\n"
      << "  --no-minimalloc                 Do not execute the exact baseline\n"
      << "  --no-core-relaxations           Do not derive PyPTO lower-bound instances\n"
      << "  --help                           Show this help\n";
}

Options ParseOptions(int argc, char** argv) {
  Options options;
  auto next = [&](int* index, const std::string& option) -> std::string_view {
    if (*index + 1 >= argc) UsageError(option + " requires a value");
    ++*index;
    return argv[*index];
  };

  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--help") {
      PrintHelp();
      std::exit(0);
    } else if (option == "--standard") {
      options.standard_roots.emplace_back(next(&index, option));
    } else if (option == "--pypto") {
      options.pypto_roots.emplace_back(next(&index, option));
    } else if (option == "--output-dir") {
      options.output_dir = fs::path(next(&index, option));
    } else if (option == "--run-label") {
      options.run_label = next(&index, option);
    } else if (option == "--standard-capacity") {
      options.standard_capacity = ParseUnsigned(next(&index, option), option);
    } else if (option == "--seeds") {
      options.seeds = ParseSeeds(next(&index, option));
    } else if (option == "--iterations") {
      options.iterations = ParseSize(next(&index, option), option);
    } else if (option == "--restarts") {
      options.restarts = ParseSize(next(&index, option), option);
    } else if (option == "--stagnation") {
      options.stagnation_limit = ParseSize(next(&index, option), option);
    } else if (option == "--minimalloc-timeout-ms") {
      options.minimalloc_timeout_ms = ParseUnsigned(next(&index, option), option);
    } else if (option == "--no-minimalloc") {
      options.run_minimalloc = false;
    } else if (option == "--no-core-relaxations") {
      options.build_core_relaxations = false;
    } else {
      UsageError("unknown option '" + option + "'");
    }
  }

  if (options.standard_roots.empty() && options.pypto_roots.empty()) {
    UsageError("at least one --standard or --pypto input is required");
  }
  if (options.output_dir.empty()) UsageError("--output-dir cannot be empty");
  if (options.run_label.empty()) UsageError("--run-label cannot be empty");
  if (options.restarts == 0) UsageError("--restarts must be positive");
  if (options.minimalloc_timeout_ms == 0) {
    UsageError("--minimalloc-timeout-ms must be positive");
  }
  return options;
}

bool HasExtension(const fs::path& path, const std::vector<std::string>& extensions) {
  const std::string extension = path.extension().string();
  return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

std::vector<fs::path> DiscoverFiles(const std::vector<fs::path>& roots,
                                    const std::vector<std::string>& extensions) {
  std::vector<fs::path> files;
  for (const fs::path& root : roots) {
    if (!fs::exists(root)) throw std::runtime_error("input path does not exist: " + root.string());
    const std::size_t before = files.size();
    if (fs::is_regular_file(root)) {
      if (!HasExtension(root, extensions)) {
        throw std::runtime_error("input file has an unsupported extension: " + root.string());
      }
      files.push_back(root.lexically_normal());
    } else if (fs::is_directory(root)) {
      for (const fs::directory_entry& entry :
           fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file() && HasExtension(entry.path(), extensions)) {
          files.push_back(entry.path().lexically_normal());
        }
      }
    } else {
      throw std::runtime_error("input path is neither a file nor a directory: " + root.string());
    }
    if (files.size() == before) {
      throw std::runtime_error("input path contains no matching benchmark files: " + root.string());
    }
  }
  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  return files;
}

std::string DisplayPath(const fs::path& path) {
  std::error_code error;
  const fs::path absolute = fs::absolute(path, error);
  if (!error) {
    const fs::path relative = fs::relative(absolute, fs::current_path(), error);
    if (!error && !relative.empty()) {
      const std::string text = relative.generic_string();
      if (text != ".." && text.rfind("../", 0) != 0) return text;
    }
  }
  return path.filename().generic_string();
}

std::vector<std::string> ProblemFeatures(const dsa::DsaProblem& problem) {
  std::vector<std::string> features;
  const std::size_t multi_interval = static_cast<std::size_t>(
      std::count_if(problem.buffers.begin(), problem.buffers.end(),
                    [](const dsa::Buffer& buffer) { return buffer.live_intervals.size() > 1; }));
  if (multi_interval != 0) features.push_back("multi_interval=" + std::to_string(multi_interval));
  if (problem.pools.size() > 1) features.push_back("pools=" + std::to_string(problem.pools.size()));
  if (!problem.colocations.empty()) {
    features.push_back("colocations=" + std::to_string(problem.colocations.size()));
  }
  if (!problem.separations.empty()) {
    features.push_back("separations=" + std::to_string(problem.separations.size()));
  }
  if (!problem.temporal_exclusions.empty()) {
    features.push_back("temporal_exclusions=" + std::to_string(problem.temporal_exclusions.size()));
  }
  if (!problem.pinned_allocations.empty()) {
    features.push_back("pins=" + std::to_string(problem.pinned_allocations.size()));
  }
  if (problem.cost_model && !problem.cost_model->reuse_penalties.empty()) {
    features.push_back("reuse_edges=" + std::to_string(problem.cost_model->reuse_penalties.size()));
  }
  if (problem.pypto_structure) {
    if (problem.pypto_structure->whole_slot_reuse) features.push_back("whole_slot_reuse");
    if (!problem.pypto_structure->alias_classes.empty()) {
      features.push_back("alias_classes=" +
                         std::to_string(problem.pypto_structure->alias_classes.size()));
    }
    if (!problem.pypto_structure->pipeline_groups.empty()) {
      features.push_back("pipeline_groups=" +
                         std::to_string(problem.pypto_structure->pipeline_groups.size()));
    }
  }
  return features;
}

FeatureStats AnalyzeFeatures(const Instance& instance) {
  FeatureStats stats;
  stats.instance = instance.document.instance;
  stats.profile = dsa::ToString(instance.document.profile);
  const dsa::DsaProblem& problem = instance.document.problem;
  stats.buffers = problem.buffers.size();
  stats.pools = problem.pools.size();
  for (const dsa::Buffer& buffer : problem.buffers) {
    if (buffer.alignment > 1) ++stats.aligned_buffers;
    if (buffer.live_intervals.size() > 1) ++stats.multi_interval_buffers;
    if (buffer.allowed_pools.size() > 1) ++stats.flexible_pool_buffers;
  }
  for (const dsa::Pool& pool : problem.pools) {
    stats.reserved_ranges += pool.reserved_ranges.size();
    if (pool.bank_geometry) ++stats.bank_geometries;
  }
  stats.colocations = problem.colocations.size();
  stats.separations = problem.separations.size();
  for (const dsa::Separation& separation : problem.separations) {
    for (dsa::SeparationReason reason : separation.reasons) {
      switch (reason) {
        case dsa::SeparationReason::kPipelineStage:
          ++stats.pipeline_separations;
          break;
        case dsa::SeparationReason::kTargetHazard:
          ++stats.target_hazard_separations;
          break;
        case dsa::SeparationReason::kSemanticNoAlias:
          ++stats.semantic_no_alias_separations;
          break;
        case dsa::SeparationReason::kGeneric:
          break;
      }
    }
  }
  stats.temporal_exclusions = problem.temporal_exclusions.size();
  stats.pinned_allocations = problem.pinned_allocations.size();
  if (problem.cost_model) stats.reuse_penalties = problem.cost_model->reuse_penalties.size();
  if (problem.pypto_structure) {
    stats.whole_slot_reuse = problem.pypto_structure->whole_slot_reuse;
    stats.pipeline_groups = problem.pypto_structure->pipeline_groups.size();
    for (const dsa::PyptoAliasClass& alias_class : problem.pypto_structure->alias_classes) {
      if (alias_class.members.size() > 1) ++stats.nontrivial_alias_classes;
    }
    for (const dsa::PyptoPipelineGroup& group : problem.pypto_structure->pipeline_groups) {
      if (group.effective_depth < group.depth) ++stats.depth_shed_pipeline_groups;
    }
  }
  return stats;
}

std::string MetadataValue(const dsa::StructuredProblemDocument& document, const std::string& key) {
  const auto found = document.metadata.find(key);
  return found == document.metadata.end() ? std::string{} : found->second;
}

Instance LoadStandardInstance(const fs::path& path,
                              std::optional<std::uint64_t> capacity_override) {
  Instance instance;
  instance.source = DisplayPath(path);
  if (path.extension() == ".json") {
    instance.document = dsa::ReadStructuredProblemJsonFile(path);
    if (instance.document.profile != dsa::BenchmarkProfile::kStandardDsa &&
        instance.document.profile != dsa::BenchmarkProfile::kPyptoCoreRelaxation) {
      throw std::runtime_error("--standard JSON does not use a standard profile: " + path.string());
    }
  } else {
    dsa::MiniMallocDocument csv = dsa::ReadMiniMallocCsvFile(path);
    instance.document.profile = dsa::BenchmarkProfile::kStandardDsa;
    instance.document.instance = path.filename().string();
    instance.document.metadata["source_format"] = "minimalloc_csv";
    instance.document.problem = std::move(csv.problem);
  }
  if (capacity_override && path.extension() != ".json") {
    if (instance.document.problem.pools.size() != 1) {
      throw std::runtime_error("standard capacity override requires exactly one pool");
    }
    instance.document.problem.pools.front().capacity = capacity_override;
  }
  const std::vector<std::string> errors = dsa::ValidateStructuredProblemDocument(instance.document);
  if (!errors.empty()) {
    throw std::runtime_error("invalid standard benchmark '" + path.string() +
                             "': " + errors.front());
  }
  return instance;
}

Instance LoadPyptoInstance(const fs::path& path) {
  Instance instance;
  instance.source = DisplayPath(path);
  instance.document = dsa::ReadStructuredProblemJsonFile(path);
  if (!dsa::IsPyptoProfile(instance.document.profile)) {
    throw std::runtime_error("--pypto JSON does not use a PyPTO profile: " + path.string());
  }
  const std::vector<std::string> errors = dsa::ValidateStructuredProblemDocument(instance.document);
  if (!errors.empty()) {
    throw std::runtime_error("invalid PyPTO benchmark '" + path.string() + "': " + errors.front());
  }
  return instance;
}

std::vector<Instance> LoadInstances(const Options& options) {
  std::vector<Instance> instances;
  for (const fs::path& path : DiscoverFiles(options.standard_roots, {".csv", ".json"})) {
    instances.push_back(LoadStandardInstance(path, options.standard_capacity));
  }
  for (const fs::path& path : DiscoverFiles(options.pypto_roots, {".json"})) {
    Instance structured = LoadPyptoInstance(path);
    instances.push_back(structured);
    if (!options.build_core_relaxations) continue;
    try {
      for (dsa::StructuredProblemDocument& relaxation :
           dsa::BuildCoreRelaxations(structured.document)) {
        instances.push_back({std::move(relaxation), structured.source});
      }
    } catch (const std::exception& error) {
      std::cerr << "dsa-suite: lower bounds unavailable for " << structured.document.instance
                << ": " << error.what() << '\n';
    }
  }
  std::sort(instances.begin(), instances.end(), [](const Instance& first, const Instance& second) {
    return std::tie(first.document.profile, first.document.instance) <
           std::tie(second.document.profile, second.document.instance);
  });
  for (std::size_t index = 1; index < instances.size(); ++index) {
    if (instances[index - 1].document.profile == instances[index].document.profile &&
        instances[index - 1].document.instance == instances[index].document.instance) {
      throw std::runtime_error("duplicate benchmark identity '" +
                               instances[index].document.instance + "' in profile " +
                               dsa::ToString(instances[index].document.profile));
    }
  }
  return instances;
}

std::string ComparisonScope(dsa::BenchmarkProfile profile) {
  switch (profile) {
    case dsa::BenchmarkProfile::kStandardDsa:
      return "direct_standard";
    case dsa::BenchmarkProfile::kPyptoStructured:
      return "pypto_legacy";
    case dsa::BenchmarkProfile::kPyptoHardV1:
      return "pypto_hard";
    case dsa::BenchmarkProfile::kPyptoResearchV1:
      return "pypto_research";
    case dsa::BenchmarkProfile::kPyptoCoreRelaxation:
      return "core_lower_bound";
  }
  return "unknown";
}

RunRecord BaseRecord(const Instance& instance, std::string method) {
  RunRecord record;
  const dsa::StructuredProblemDocument& document = instance.document;
  record.instance = document.instance;
  record.profile = dsa::ToString(document.profile);
  record.source = instance.source;
  record.method = std::move(method);
  record.comparison_scope = ComparisonScope(document.profile);
  record.relaxed_from = document.relaxed_from;
  if (document.problem.pools.size() == 1) {
    record.capacity = document.problem.pools.front().capacity;
  }
  record.buffers = document.problem.buffers.size();
  record.target = MetadataValue(document, "target");
  record.features = ProblemFeatures(document.problem);
  record.relaxed_features = document.relaxed_features;
  record.metadata = document.metadata;
  return record;
}

std::uint64_t DurationMicros(std::chrono::steady_clock::duration duration) {
  const auto count = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
  return count <= 0 ? 0 : static_cast<std::uint64_t>(count);
}

void PopulateSolutionMetrics(const dsa::DsaProblem& problem, const dsa::DsaResult& result,
                             RunRecord* record) {
  if (!result.solution) return;
  const std::vector<std::string> original_validation =
      dsa::ValidateSolution(problem, *result.solution);
  record->solution_valid = original_validation.empty();
  dsa::DsaProblem placement_problem = problem;
  if (result.status == dsa::SolveStatus::kBestEffortNoFit) {
    for (dsa::Pool& pool : placement_problem.pools) pool.capacity.reset();
  }
  const std::vector<std::string> placement_validation =
      dsa::ValidateSolution(placement_problem, *result.solution);
  record->placement_valid = placement_validation.empty();
  for (const std::string& error : placement_validation) {
    record->diagnostics.push_back("independent validation: " + error);
  }
  if (!record->placement_valid) return;

  const dsa::ObjectiveValue objective = dsa::EvaluateObjective(problem, *result.solution);
  if (objective.peak_by_pool != result.objective.peak_by_pool ||
      objective.total_peak != result.objective.total_peak ||
      objective.max_peak != result.objective.max_peak ||
      objective.reuse_cost != result.objective.reuse_cost ||
      objective.bank_cost != result.objective.bank_cost) {
    record->diagnostics.push_back(
        "independent objective recomputation differs from the solver-reported objective");
  }
  record->peak = objective.max_peak;
  record->total_peak = objective.total_peak;
  record->capacity_overflow =
      dsa::EvaluateObjectiveMetric(problem, objective, dsa::ObjectiveMetric::kCapacityOverflow);
  record->reuse_cost = objective.reuse_cost;
  record->bank_cost = objective.bank_cost;
  for (dsa::ObjectiveMetric metric : problem.objective.terms) {
    record->objective_score.push_back(dsa::EvaluateObjectiveMetric(problem, objective, metric));
  }
}

RunRecord RunHeuristic(const Instance& instance, std::string_view method,
                       std::optional<std::uint64_t> seed, const Options& options) {
  std::unique_ptr<dsa::DsaSolver> solver;
  if (method == kFirstFit) {
    solver = std::make_unique<dsa::FirstFitSolver>();
  } else if (method == kXlaHeap) {
    solver = std::make_unique<dsa::XlaHeapSolver>();
  } else if (method == kTvmHillClimb) {
    dsa::TvmHillClimbOptions solver_options;
    solver_options.seed = seed.value_or(0);
    solver_options.max_attempts = options.iterations;
    solver = std::make_unique<dsa::TvmHillClimbSolver>(solver_options);
  } else if (method == kLocalSearch) {
    dsa::LocalSearchOptions solver_options;
    solver_options.seed = seed.value_or(0);
    solver_options.max_iterations = options.iterations;
    solver_options.restarts = options.restarts;
    solver_options.stagnation_limit = options.stagnation_limit;
    solver = std::make_unique<dsa::LocalSearchSolver>(solver_options);
  } else if (method == kPyptoStructuredSearch) {
    dsa::PyptoStructuredSearchOptions solver_options;
    solver_options.seed = seed.value_or(0);
    solver_options.max_iterations = options.iterations;
    solver_options.restarts = options.restarts;
    solver_options.stagnation_limit = options.stagnation_limit;
    solver = std::make_unique<dsa::PyptoStructuredSearchSolver>(solver_options);
  } else {
    throw std::logic_error("unknown in-process heuristic");
  }

  RunRecord record = BaseRecord(instance, std::string(method));
  record.seed = seed;
  const dsa::SolverCompatibility compatibility =
      dsa::CheckSolverCompatibility(instance.document.problem, solver->Capabilities());
  record.structurally_compatible = compatibility.StructurallyCompatible();
  record.objective_compatible = compatibility.ObjectiveCompatible();

  const auto started = std::chrono::steady_clock::now();
  const dsa::DsaResult result = solver->Solve(instance.document.problem);
  const auto stopped = std::chrono::steady_clock::now();
  record.runtime_us = DurationMicros(stopped - started);
  record.status = dsa::ToString(result.status);
  record.diagnostics = result.diagnostics;
  PopulateSolutionMetrics(instance.document.problem, result, &record);
  return record;
}

#ifdef DSA_HAVE_MINIMALLOC
std::optional<std::uint64_t> ExactUpperBound(const dsa::DsaProblem& problem) {
  if (problem.pools.size() != 1) return std::nullopt;
  if (problem.pools.front().capacity) return problem.pools.front().capacity;
  std::uint64_t sum = 0;
  for (const dsa::Buffer& buffer : problem.buffers) {
    if (buffer.size > std::numeric_limits<std::uint64_t>::max() - sum) return std::nullopt;
    sum += buffer.size;
  }
  return sum;
}

RunRecord RunMiniMalloc(const Instance& instance, const Options& options) {
  RunRecord record = BaseRecord(instance, std::string(kMiniMalloc));
  if (!options.run_minimalloc) {
    record.status = "not_run";
    record.diagnostics.push_back("MiniMalloc execution disabled by --no-minimalloc");
    return record;
  }
  const std::optional<std::uint64_t> upper_bound = ExactUpperBound(instance.document.problem);
  if (!upper_bound ||
      *upper_bound > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    record.status = "unsupported";
    record.diagnostics.push_back("exact upper bound exceeds MiniMalloc's signed capacity range");
    return record;
  }
  if (options.minimalloc_timeout_ms >
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    record.status = "unsupported";
    record.diagnostics.push_back("MiniMalloc timeout exceeds Abseil's signed duration range");
    return record;
  }

  minimalloc::Problem problem;
  problem.capacity = static_cast<std::int64_t>(*upper_bound);
  problem.buffers.reserve(instance.document.problem.buffers.size());
  for (const dsa::Buffer& source : instance.document.problem.buffers) {
    if (source.live_intervals.size() != 1 ||
        source.size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        source.alignment > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      record.status = "unsupported";
      record.diagnostics.push_back("instance is outside MiniMalloc's standard numeric model");
      return record;
    }
    minimalloc::Buffer buffer;
    buffer.id = source.name;
    buffer.lifespan = {source.live_intervals.front().lower, source.live_intervals.front().upper};
    buffer.size = static_cast<std::int64_t>(source.size);
    buffer.alignment = static_cast<std::int64_t>(source.alignment);
    problem.buffers.push_back(std::move(buffer));
  }

  minimalloc::SolverParams params;
  params.timeout = absl::Milliseconds(static_cast<std::int64_t>(options.minimalloc_timeout_ms));
  params.minimize_capacity = true;
  minimalloc::Solver solver(params);
  const auto started = std::chrono::steady_clock::now();
  const absl::StatusOr<minimalloc::Solution> solution = solver.Solve(problem);
  const auto stopped = std::chrono::steady_clock::now();
  record.runtime_us = DurationMicros(stopped - started);
  record.backtracks =
      static_cast<std::uint64_t>(std::max<std::int64_t>(0, solver.get_backtracks()));

  const std::uint64_t timeout_us =
      options.minimalloc_timeout_ms > std::numeric_limits<std::uint64_t>::max() / 1'000
          ? std::numeric_limits<std::uint64_t>::max()
          : options.minimalloc_timeout_ms * 1'000;
  const std::uint64_t tolerance_us = std::max<std::uint64_t>(1'000, timeout_us / 100);
  const bool exhausted_budget =
      record.runtime_us >= (timeout_us > tolerance_us ? timeout_us - tolerance_us : 0);
  const bool timed_out = exhausted_budget || absl::IsDeadlineExceeded(solution.status());

  if (!solution.ok()) {
    record.status =
        timed_out ? "timeout" : (absl::IsNotFound(solution.status()) ? "infeasible" : "error");
    record.diagnostics.push_back(solution.status().ToString());
    return record;
  }
  if (solution->offsets.size() != instance.document.problem.buffers.size()) {
    record.status = "error";
    record.diagnostics.push_back("MiniMalloc returned the wrong number of offsets");
    return record;
  }

  dsa::DsaSolution converted;
  for (std::size_t index = 0; index < solution->offsets.size(); ++index) {
    if (solution->offsets[index] < 0) {
      record.status = "error";
      record.diagnostics.push_back("MiniMalloc returned a negative offset");
      return record;
    }
    converted.placements.emplace(
        instance.document.problem.buffers[index].id,
        dsa::Placement{instance.document.problem.pools.front().id,
                       static_cast<std::uint64_t>(solution->offsets[index])});
  }
  const std::vector<std::string> validation =
      dsa::ValidateSolution(instance.document.problem, converted);
  record.placement_valid = validation.empty();
  record.solution_valid = validation.empty();
  for (const std::string& error : validation) {
    record.diagnostics.push_back("independent validation: " + error);
  }
  if (!record.solution_valid) {
    record.status = "error";
    return record;
  }
  const dsa::ObjectiveValue objective =
      dsa::EvaluateObjective(instance.document.problem, converted);
  record.peak = objective.max_peak;
  record.total_peak = objective.total_peak;
  record.capacity_overflow = dsa::EvaluateObjectiveMetric(instance.document.problem, objective,
                                                          dsa::ObjectiveMetric::kCapacityOverflow);
  record.reuse_cost = objective.reuse_cost;
  record.bank_cost = objective.bank_cost;
  record.objective_score = {*record.peak};
  record.certified_optimal = !timed_out;
  record.status = timed_out ? "timeout_with_upper_bound" : "optimal";
  if (timed_out) {
    record.diagnostics.push_back(
        "MiniMalloc returned a feasible upper bound but exhausted the exact-solver budget");
  }
  return record;
}
#else
RunRecord RunMiniMalloc(const Instance& instance, const Options& options) {
  RunRecord record = BaseRecord(instance, std::string(kMiniMalloc));
  record.status = options.run_minimalloc ? "unavailable" : "not_run";
  record.diagnostics.push_back(options.run_minimalloc
                                   ? "dsa-suite was built without DSA_ENABLE_MINIMALLOC_BASELINE"
                                   : "MiniMalloc execution disabled by --no-minimalloc");
  return record;
}
#endif

std::vector<RunRecord> ExecuteSuite(const std::vector<Instance>& instances,
                                    const Options& options) {
  std::vector<RunRecord> records;
  for (const Instance& instance : instances) {
    std::cout << "[suite] " << instance.document.instance << " ["
              << dsa::ToString(instance.document.profile) << "]" << std::endl;
    records.push_back(RunHeuristic(instance, kFirstFit, std::nullopt, options));
    records.push_back(RunHeuristic(instance, kXlaHeap, std::nullopt, options));
    for (std::uint64_t seed : options.seeds) {
      records.push_back(RunHeuristic(instance, kTvmHillClimb, seed, options));
      records.push_back(RunHeuristic(instance, kLocalSearch, seed, options));
      if (dsa::IsPyptoProfile(instance.document.profile)) {
        records.push_back(RunHeuristic(instance, kPyptoStructuredSearch, seed, options));
      }
    }
    if (!dsa::IsPyptoProfile(instance.document.profile)) {
      records.push_back(RunMiniMalloc(instance, options));
    }
  }
  return records;
}

bool HasUsableSolution(const RunRecord& record) {
  return record.placement_valid && record.peak.has_value();
}

bool IsFeasible(const RunRecord& record) {
  return record.solution_valid && (record.status == "feasible" || record.status == "optimal");
}

bool BetterRecord(const RunRecord& first, const RunRecord& second) {
  if (IsFeasible(first) != IsFeasible(second)) return IsFeasible(first);
  if (HasUsableSolution(first) != HasUsableSolution(second)) return HasUsableSolution(first);
  if (first.objective_score != second.objective_score) {
    return std::lexicographical_compare(first.objective_score.begin(), first.objective_score.end(),
                                        second.objective_score.begin(),
                                        second.objective_score.end());
  }
  return first.runtime_us < second.runtime_us;
}

using SummaryKey = std::tuple<std::string, std::string, std::string, std::string>;

std::vector<Summary> Summarize(const std::vector<RunRecord>& records) {
  std::map<SummaryKey, std::vector<const RunRecord*>> groups;
  for (const RunRecord& record : records) {
    groups[{record.instance, record.profile, record.method, record.comparison_scope}].push_back(
        &record);
  }

  std::vector<Summary> summaries;
  summaries.reserve(groups.size());
  for (const auto& [key, group] : groups) {
    Summary summary;
    summary.instance = std::get<0>(key);
    summary.profile = std::get<1>(key);
    summary.method = std::get<2>(key);
    summary.comparison_scope = std::get<3>(key);
    summary.source = group.front()->source;
    const auto family = group.front()->metadata.find("corpus_family");
    if (family != group.front()->metadata.end()) summary.corpus_family = family->second;
    const auto source_path = group.front()->metadata.find("corpus_source_path");
    if (source_path != group.front()->metadata.end()) {
      summary.corpus_source_path = source_path->second;
    }
    summary.relaxed_from = group.front()->relaxed_from;
    summary.capacity = group.front()->capacity;
    summary.target = group.front()->target;
    summary.features = group.front()->features;
    summary.runs = group.size();
    std::vector<std::uint64_t> runtimes;
    for (const RunRecord* record : group) {
      if (IsFeasible(*record)) ++summary.feasible_runs;
      if (record->placement_valid) ++summary.placement_valid_runs;
      if (record->solution_valid) ++summary.valid_runs;
      if (record->status != "not_run" && record->status != "unavailable") {
        runtimes.push_back(record->runtime_us);
      }
      if (!summary.best || BetterRecord(*record, *summary.best)) summary.best = *record;
      summary.certified_optimal = summary.certified_optimal || record->certified_optimal;
    }
    if (!runtimes.empty()) {
      std::sort(runtimes.begin(), runtimes.end());
      summary.median_runtime_us = runtimes[runtimes.size() / 2];
    }
    summary.status = summary.best ? summary.best->status : "missing";
    summaries.push_back(std::move(summary));
  }
  return summaries;
}

void PutOptional(Json* json, const std::string& key, const std::optional<std::uint64_t>& value) {
  if (value) {
    (*json)[key] = *value;
  } else {
    (*json)[key] = nullptr;
  }
}

Json RecordJson(const RunRecord& record, const Options& options) {
  Json json;
  json["run_label"] = options.run_label;
  json["instance"] = record.instance;
  json["schema_version"] = dsa::kStructuredProblemSchemaVersion;
  json["profile"] = record.profile;
  json["source"] = record.source;
  json["method"] = record.method;
  json["comparison_scope"] = record.comparison_scope;
  json["status"] = record.status;
  json["buffers"] = record.buffers;
  PutOptional(&json, "capacity", record.capacity);
  PutOptional(&json, "peak", record.peak);
  PutOptional(&json, "total_peak", record.total_peak);
  PutOptional(&json, "capacity_overflow", record.capacity_overflow);
  PutOptional(&json, "reuse_cost", record.reuse_cost);
  PutOptional(&json, "bank_cost", record.bank_cost);
  PutOptional(&json, "backtracks", record.backtracks);
  PutOptional(&json, "seed", record.seed);
  json["runtime_us"] = record.runtime_us;
  json["placement_valid"] = record.placement_valid;
  json["solution_valid"] = record.solution_valid;
  json["structurally_compatible"] = record.structurally_compatible;
  json["objective_compatible"] = record.objective_compatible;
  json["certified_optimal"] = record.certified_optimal;
  json["target"] = record.target.empty() ? Json(nullptr) : Json(record.target);
  json["features"] = record.features;
  json["relaxed_features"] = record.relaxed_features;
  json["diagnostics"] = record.diagnostics;
  json["objective_score"] = record.objective_score;
  json["metadata"] = record.metadata;
  json["relaxed_from"] = record.relaxed_from ? Json(*record.relaxed_from) : Json(nullptr);
  return json;
}

std::string CsvEscape(std::string_view value) {
  if (value.find_first_of(",\"\n\r") == std::string_view::npos) return std::string(value);
  std::string result = "\"";
  for (char character : value) {
    if (character == '\"') result.push_back('\"');
    result.push_back(character);
  }
  result.push_back('\"');
  return result;
}

std::string Join(const std::vector<std::string>& values, std::string_view separator) {
  std::ostringstream output;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) output << separator;
    output << values[index];
  }
  return output.str();
}

std::string OptionalCsv(const std::optional<std::uint64_t>& value) {
  return value ? std::to_string(*value) : std::string{};
}

void WriteRawResults(const fs::path& path, const std::vector<RunRecord>& records,
                     const Options& options) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot write raw results: " + path.string());
  for (const RunRecord& record : records) output << RecordJson(record, options).dump() << '\n';
}

void WriteSummaryCsv(const fs::path& path, const std::vector<Summary>& summaries) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot write summary CSV: " + path.string());
  output << "instance,profile,source,corpus_family,corpus_source_path,method,comparison_scope,"
            "status,runs,feasible_runs,"
            "placement_valid_runs,valid_runs,"
            "best_seed,best_peak,best_total_peak,best_capacity_overflow,best_reuse_cost,"
            "median_runtime_us,certified_optimal,capacity,target,features,relaxed_from\n";
  for (const Summary& summary : summaries) {
    const RunRecord* best = summary.best ? &*summary.best : nullptr;
    output << CsvEscape(summary.instance) << ',' << CsvEscape(summary.profile) << ','
           << CsvEscape(summary.source) << ',' << CsvEscape(summary.corpus_family) << ','
           << CsvEscape(summary.corpus_source_path) << ',' << CsvEscape(summary.method) << ','
           << CsvEscape(summary.comparison_scope) << ',' << CsvEscape(summary.status) << ','
           << summary.runs << ',' << summary.feasible_runs << ',' << summary.placement_valid_runs
           << ',' << summary.valid_runs << ',' << (best ? OptionalCsv(best->seed) : std::string{})
           << ',' << (best ? OptionalCsv(best->peak) : std::string{}) << ','
           << (best ? OptionalCsv(best->total_peak) : std::string{}) << ','
           << (best ? OptionalCsv(best->capacity_overflow) : std::string{}) << ','
           << (best ? OptionalCsv(best->reuse_cost) : std::string{}) << ','
           << summary.median_runtime_us << ',' << (summary.certified_optimal ? "true" : "false")
           << ',' << OptionalCsv(summary.capacity) << ',' << CsvEscape(summary.target) << ','
           << CsvEscape(Join(summary.features, ";")) << ','
           << CsvEscape(summary.relaxed_from.value_or("")) << '\n';
  }
}

void WriteFeaturesCsv(const fs::path& path, const std::vector<Instance>& instances) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot write feature CSV: " + path.string());
  output << "instance,profile,buffers,pools,aligned_buffers,multi_interval_buffers,"
            "flexible_pool_buffers,reserved_ranges,bank_geometries,colocations,separations,"
            "pipeline_separations,target_hazard_separations,semantic_no_alias_separations,"
            "temporal_exclusions,pinned_allocations,reuse_penalties,whole_slot_reuse,"
            "nontrivial_alias_classes,pipeline_groups,depth_shed_pipeline_groups\n";
  for (const Instance& instance : instances) {
    if (instance.document.profile == dsa::BenchmarkProfile::kPyptoCoreRelaxation) continue;
    const FeatureStats stats = AnalyzeFeatures(instance);
    output << CsvEscape(stats.instance) << ',' << CsvEscape(stats.profile) << ',' << stats.buffers
           << ',' << stats.pools << ',' << stats.aligned_buffers << ','
           << stats.multi_interval_buffers << ',' << stats.flexible_pool_buffers << ','
           << stats.reserved_ranges << ',' << stats.bank_geometries << ',' << stats.colocations
           << ',' << stats.separations << ',' << stats.pipeline_separations << ','
           << stats.target_hazard_separations << ',' << stats.semantic_no_alias_separations << ','
           << stats.temporal_exclusions << ',' << stats.pinned_allocations << ','
           << stats.reuse_penalties << ',' << (stats.whole_slot_reuse ? "true" : "false") << ','
           << stats.nontrivial_alias_classes << ',' << stats.pipeline_groups << ','
           << stats.depth_shed_pipeline_groups << '\n';
  }
}

std::string MarkdownEscape(std::string value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char character : value) {
    if (character == '\n' || character == '\r') {
      escaped.push_back(' ');
      continue;
    }
    if (character == '|') escaped.push_back('\\');
    escaped.push_back(character);
  }
  return escaped;
}

std::string ShellQuote(std::string_view value) {
  std::string quoted = "'";
  for (char character : value) {
    if (character == '\'') {
      quoted += "'\"'\"'";
    } else {
      quoted.push_back(character);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::string FormatRuntime(std::uint64_t microseconds) {
  std::ostringstream output;
  if (microseconds < 1'000) {
    output << microseconds << " us";
  } else if (microseconds < 1'000'000) {
    output << std::fixed << std::setprecision(2)
           << static_cast<long double>(microseconds) / 1'000.0L << " ms";
  } else {
    output << std::fixed << std::setprecision(2)
           << static_cast<long double>(microseconds) / 1'000'000.0L << " s";
  }
  return output.str();
}

const Summary* FindSummary(const std::vector<Summary>& summaries, std::string_view instance,
                           std::string_view profile, std::string_view method) {
  const auto found = std::find_if(summaries.begin(), summaries.end(), [&](const Summary& summary) {
    return summary.instance == instance && summary.profile == profile && summary.method == method;
  });
  return found == summaries.end() ? nullptr : &*found;
}

std::string ExactCell(const Summary* summary) {
  if (summary == nullptr || !summary->best) return "—";
  const RunRecord& best = *summary->best;
  if (!HasUsableSolution(best)) return MarkdownEscape(best.status);
  std::ostringstream output;
  if (!best.certified_optimal) output << "≤";
  output << *best.peak << " (" << (best.certified_optimal ? "optimal" : best.status) << "; "
         << FormatRuntime(summary->median_runtime_us) << ')';
  return output.str();
}

std::string HeuristicCell(const Summary* summary, const Summary* exact, bool structured) {
  if (summary == nullptr || !summary->best) return "—";
  const RunRecord& best = *summary->best;
  if (!HasUsableSolution(best)) return MarkdownEscape(best.status);
  std::ostringstream output;
  if (structured) {
    output << "P=" << *best.peak;
    if (best.reuse_cost) output << ", C=" << *best.reuse_cost;
  } else {
    output << *best.peak;
    if (exact != nullptr && exact->best && exact->best->certified_optimal && exact->best->peak &&
        *exact->best->peak != 0) {
      const long double gap =
          100.0L *
          (static_cast<long double>(*best.peak) - static_cast<long double>(*exact->best->peak)) /
          static_cast<long double>(*exact->best->peak);
      output << " (" << std::showpos << std::fixed << std::setprecision(2) << gap << "%"
             << std::noshowpos << "; " << FormatRuntime(summary->median_runtime_us) << ')';
    } else {
      output << " (" << FormatRuntime(summary->median_runtime_us) << ')';
    }
  }
  if (structured) {
    output << " (" << best.status << "; " << FormatRuntime(summary->median_runtime_us);
    if (!best.objective_compatible) output << "; structural-only";
    output << ')';
  } else if (best.status != "feasible") {
    output << " [" << MarkdownEscape(best.status) << ']';
  }
  return output.str();
}

std::string CoreLowerBoundCell(const std::vector<Summary>& summaries, std::string_view instance) {
  std::vector<const Summary*> bounds;
  for (const Summary& summary : summaries) {
    if (summary.profile == dsa::ToString(dsa::BenchmarkProfile::kPyptoCoreRelaxation) &&
        summary.method == kMiniMalloc && summary.relaxed_from &&
        *summary.relaxed_from == instance) {
      bounds.push_back(&summary);
    }
  }
  if (bounds.empty()) return "—";
  std::uint64_t total = 0;
  std::uint64_t maximum = 0;
  for (const Summary* summary : bounds) {
    if (!summary->certified_optimal || !summary->best || !summary->best->peak) {
      return "not certified";
    }
    if (*summary->best->peak > std::numeric_limits<std::uint64_t>::max() - total) {
      return "overflow";
    }
    total += *summary->best->peak;
    maximum = std::max(maximum, *summary->best->peak);
  }
  std::ostringstream output;
  output << "sum=" << total << ", max=" << maximum;
  output << " (optimal relaxations)";
  return output.str();
}

void WriteFeatureOccurrenceReport(std::ostream& output, const std::vector<Instance>& instances) {
  std::vector<FeatureStats> rows;
  for (const Instance& instance : instances) {
    if (dsa::IsPyptoProfile(instance.document.profile)) rows.push_back(AnalyzeFeatures(instance));
  }
  output
      << "## PyPTO feature occurrence\n\n"
      << "Counts are computed from unique input documents, before core relaxations. "
         "A zero is evidence that the current corpus does not exercise the feature; it is not "
         "evidence that the feature is unnecessary. Per-instance values are in `features.csv`.\n\n"
      << "| Feature | Documents | Total occurrences |\n"
      << "| --- | ---: | ---: |\n";
  auto write_count = [&](std::string_view name, std::size_t FeatureStats::* member) {
    std::size_t documents = 0;
    std::size_t total = 0;
    for (const FeatureStats& row : rows) {
      const std::size_t value = row.*member;
      if (value != 0) ++documents;
      total += value;
    }
    output << "| " << name << " | " << documents << " | " << total << " |\n";
  };
  std::size_t whole_slot_documents = 0;
  for (const FeatureStats& row : rows) whole_slot_documents += row.whole_slot_reuse ? 1 : 0;
  output << "| whole-slot reuse | " << whole_slot_documents << " | " << whole_slot_documents
         << " |\n";
  write_count("aligned buffers", &FeatureStats::aligned_buffers);
  write_count("reserved ranges", &FeatureStats::reserved_ranges);
  write_count("separations", &FeatureStats::separations);
  write_count("pipeline-stage separation reasons", &FeatureStats::pipeline_separations);
  write_count("target-hazard separation reasons", &FeatureStats::target_hazard_separations);
  write_count("semantic-no-alias separation reasons", &FeatureStats::semantic_no_alias_separations);
  write_count("nontrivial alias provenance", &FeatureStats::nontrivial_alias_classes);
  write_count("pipeline groups", &FeatureStats::pipeline_groups);
  write_count("depth-shed pipeline groups", &FeatureStats::depth_shed_pipeline_groups);
  write_count("experimental reuse penalties", &FeatureStats::reuse_penalties);
  write_count("multi-interval buffers", &FeatureStats::multi_interval_buffers);
  write_count("flexible-pool buffers", &FeatureStats::flexible_pool_buffers);
  write_count("colocations", &FeatureStats::colocations);
  write_count("temporal exclusions", &FeatureStats::temporal_exclusions);
  write_count("pinned allocations", &FeatureStats::pinned_allocations);
  write_count("bank geometries", &FeatureStats::bank_geometries);
  output << '\n';
}

void WriteReport(const fs::path& path, const std::vector<Instance>& instances,
                 const std::vector<Summary>& summaries, const Options& options) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot write Markdown report: " + path.string());
  output << "# DSA benchmark results\n\n"
         << "Generated by `dsa-suite` (run label: " << MarkdownEscape(options.run_label)
         << "). Raw per-run data is in `results.jsonl`; aggregated long-form data is in "
            "`summary.csv`, and per-instance constraint counts are in `features.csv`.\n\n"
         << "Configuration: seeds `";
  for (std::size_t index = 0; index < options.seeds.size(); ++index) {
    if (index != 0) output << ',';
    output << options.seeds[index];
  }
  output
      << "`, search budget `" << options.iterations << "`, local-search restarts `"
      << options.restarts << "`, MiniMalloc timeout `" << options.minimalloc_timeout_ms
      << " ms` per instance. Runtime values are machine-dependent medians; placement metrics and "
         "validity are independently recomputed. `placement_valid` checks address geometry while "
         "ignoring capacity only for best-effort diagnostics; `solution_valid` includes the "
         "original capacity constraints.\n\n"
      << "Regenerate from the repository root:\n\n```bash\n./build/dsa-suite";
  auto append_option = [&](std::string_view name, const std::string& value) {
    output << " \\" << '\n' << "  --" << name << ' ' << ShellQuote(value);
  };
  for (const fs::path& root : options.standard_roots) {
    append_option("standard", root.generic_string());
  }
  for (const fs::path& root : options.pypto_roots) {
    append_option("pypto", root.generic_string());
  }
  append_option("output-dir", options.output_dir.generic_string());
  append_option("run-label", options.run_label);
  if (options.standard_capacity) {
    append_option("standard-capacity", std::to_string(*options.standard_capacity));
  }
  std::ostringstream seeds;
  for (std::size_t index = 0; index < options.seeds.size(); ++index) {
    if (index != 0) seeds << ',';
    seeds << options.seeds[index];
  }
  append_option("seeds", seeds.str());
  append_option("iterations", std::to_string(options.iterations));
  append_option("restarts", std::to_string(options.restarts));
  append_option("stagnation", std::to_string(options.stagnation_limit));
  append_option("minimalloc-timeout-ms", std::to_string(options.minimalloc_timeout_ms));
  if (!options.run_minimalloc) output << " \\" << '\n' << "  --no-minimalloc";
  if (!options.build_core_relaxations) output << " \\" << '\n' << "  --no-core-relaxations";
  output << "\n```\n\n"
         << "MiniMalloc is compared directly only with standard DSA. For PyPTO instances it runs "
            "only on explicitly recorded core relaxations, so those numbers are lower bounds—not "
            "valid structured placements. Only certified relaxation optima are shown as bounds; a "
            "timeout is never reported as a certified optimum.\n\n";
  WriteFeatureOccurrenceReport(output, instances);
  output
      << "## Standard DSA\n\n"
      << "Rows include public standard instances and compiler-derived per-pool core relaxations. "
         "A relaxation is a standard DSA problem and supports direct algorithm comparison, but its "
         "result is only a lower bound for the corresponding structured PyPTO instance.\n\n"
      << "| Instance | Origin | Buffers | Capacity | MiniMalloc exact | First fit | XLA heap | TVM "
         "hill climb | Local search |\n"
      << "| --- | --- | ---: | ---: | --- | --- | --- | --- | --- |\n";

  for (const Instance& instance : instances) {
    const dsa::StructuredProblemDocument& document = instance.document;
    if (document.profile != dsa::BenchmarkProfile::kStandardDsa &&
        document.profile != dsa::BenchmarkProfile::kPyptoCoreRelaxation) {
      continue;
    }
    const std::string profile = dsa::ToString(document.profile);
    const Summary* exact = FindSummary(summaries, document.instance, profile, kMiniMalloc);
    output << "| " << MarkdownEscape(document.instance) << " | ";
    if (document.profile == dsa::BenchmarkProfile::kPyptoCoreRelaxation) {
      output << "core relaxation of " << MarkdownEscape(document.relaxed_from.value_or("unknown"));
    } else {
      output << "public standard";
    }
    output << " | " << document.problem.buffers.size() << " | ";
    if (document.problem.pools.front().capacity) {
      output << *document.problem.pools.front().capacity;
    } else {
      output << "—";
    }
    output << " | " << ExactCell(exact) << " | "
           << HeuristicCell(FindSummary(summaries, document.instance, profile, kFirstFit), exact,
                            false)
           << " | "
           << HeuristicCell(FindSummary(summaries, document.instance, profile, kXlaHeap), exact,
                            false)
           << " | "
           << HeuristicCell(FindSummary(summaries, document.instance, profile, kTvmHillClimb),
                            exact, false)
           << " | "
           << HeuristicCell(FindSummary(summaries, document.instance, profile, kLocalSearch), exact,
                            false)
           << " |\n";
  }

  output << "\n## PyPTO structured DSA\n\n"
         << "| Instance | Profile | Family | Source | Target | Buffers | Structure | Exact core "
            "lower bound | "
            "First fit | XLA heap | TVM hill "
            "climb | Local search | PyPTO structured search |\n"
         << "| --- | --- | --- | --- | --- | ---: | --- | --- | --- | --- | --- | --- | --- |\n";
  for (const Instance& instance : instances) {
    const dsa::StructuredProblemDocument& document = instance.document;
    if (!dsa::IsPyptoProfile(document.profile)) continue;
    const std::string profile = dsa::ToString(document.profile);
    output << "| " << MarkdownEscape(document.instance) << " | " << MarkdownEscape(profile) << " | "
           << MarkdownEscape(MetadataValue(document, "corpus_family")) << " | "
           << MarkdownEscape(MetadataValue(document, "corpus_source_path")) << " | "
           << MarkdownEscape(MetadataValue(document, "target")) << " | "
           << document.problem.buffers.size() << " | "
           << MarkdownEscape(Join(ProblemFeatures(document.problem), "; ")) << " | "
           << CoreLowerBoundCell(summaries, document.instance) << " | "
           << HeuristicCell(FindSummary(summaries, document.instance, profile, kFirstFit), nullptr,
                            true)
           << " | "
           << HeuristicCell(FindSummary(summaries, document.instance, profile, kXlaHeap), nullptr,
                            true)
           << " | "
           << HeuristicCell(FindSummary(summaries, document.instance, profile, kTvmHillClimb),
                            nullptr, true)
           << " | "
           << HeuristicCell(FindSummary(summaries, document.instance, profile, kLocalSearch),
                            nullptr, true)
           << " | "
           << HeuristicCell(
                  FindSummary(summaries, document.instance, profile, kPyptoStructuredSearch),
                  nullptr, true)
           << " |\n";
  }
}

void WriteOutputs(const Options& options, const std::vector<Instance>& instances,
                  const std::vector<RunRecord>& records) {
  fs::create_directories(options.output_dir);
  const std::vector<Summary> summaries = Summarize(records);
  const fs::path raw_path = options.output_dir / "results.jsonl";
  const fs::path csv_path = options.output_dir / "summary.csv";
  const fs::path features_path = options.output_dir / "features.csv";
  const fs::path report_path = options.output_dir / "report.md";
  WriteRawResults(raw_path, records, options);
  WriteSummaryCsv(csv_path, summaries);
  WriteFeaturesCsv(features_path, instances);
  WriteReport(report_path, instances, summaries, options);
  std::cout << "dsa-suite: wrote " << raw_path << ", " << csv_path << ", " << features_path
            << ", and " << report_path << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = ParseOptions(argc, argv);
    const std::vector<Instance> instances = LoadInstances(options);
    std::cout << "dsa-suite: loaded " << instances.size() << " benchmark documents" << std::endl;
    const std::vector<RunRecord> records = ExecuteSuite(instances, options);
    WriteOutputs(options, instances, records);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "dsa-suite: " << error.what() << '\n';
    return 1;
  }
}

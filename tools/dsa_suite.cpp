// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
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
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "dsa/algorithms/cypress_relaxation_solver.h"
#include "dsa/algorithms/first_fit_solver.h"
#include "dsa/algorithms/local_search_solver.h"
#include "dsa/algorithms/pypto_structured_search_solver.h"
#include "dsa/algorithms/reuse_penalty_baseline_solvers.h"
#include "dsa/algorithms/tvm_hill_climb_solver.h"
#include "dsa/algorithms/xla_heap_solver.h"
#include "dsa/io/minimalloc_csv.h"
#include "dsa/model/structured_problem.h"
#include "dsa/model/validator.h"

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
constexpr std::string_view kCanonicalGreedy = "canonical_greedy";
constexpr std::string_view kPromoteRepair = "promote_repair";
constexpr std::string_view kCypressRelaxation = "cypress_relaxation";
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
  std::size_t deterministic_repetitions = 5;
  bool run_minimalloc = true;
  bool build_core_relaxations = true;
  bool standard_only = false;
  bool report_only = false;
  std::string run_label = "local";
};

struct Instance {
  dsa::StructuredProblemDocument document;
  std::string source;
};

struct FeatureStats {
  std::string instance;
  std::string profile;
  std::string source;
  std::string corpus_namespace;
  std::string corpus_family;
  std::string corpus_source_path;
  std::string target;
  std::vector<std::string> memory_spaces;
  std::vector<std::string> pool_capacities;
  std::vector<std::string> max_live_bytes_by_space;
  std::string tightest_space;
  std::optional<long double> max_live_capacity_ratio;
  std::size_t buffers = 0;
  std::size_t pools = 0;
  std::uint64_t min_buffer_bytes = 0;
  std::uint64_t max_buffer_bytes = 0;
  std::uint64_t total_buffer_bytes = 0;
  std::size_t distinct_buffer_sizes = 0;
  bool uniform_buffer_size = false;
  std::uint64_t min_alignment = 0;
  std::uint64_t max_alignment = 0;
  std::size_t temporal_conflicts = 0;
  std::size_t reuse_candidates = 0;
  bool live_byte_stats_complete = true;
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
  std::size_t alias_classes = 0;
  std::size_t nontrivial_alias_classes = 0;
  std::size_t pipeline_groups = 0;
  std::size_t depth_shed_pipeline_groups = 0;
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
  std::map<std::string, std::uint64_t> solver_metrics;
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
      << "  --deterministic-repetitions N  Runtime repetitions for first-fit/XLA (default: 5)\n"
      << "  --no-minimalloc                 Do not execute the exact baseline\n"
      << "  --no-core-relaxations           Do not derive PyPTO lower-bound instances\n"
      << "  --standard-only                 Project PyPTO pools to capacity-free standard DSA\n"
      << "  --report-only                   Rebuild the report from existing results.jsonl\n"
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
    } else if (option == "--deterministic-repetitions") {
      options.deterministic_repetitions = ParseSize(next(&index, option), option);
    } else if (option == "--no-minimalloc") {
      options.run_minimalloc = false;
    } else if (option == "--no-core-relaxations") {
      options.build_core_relaxations = false;
    } else if (option == "--standard-only") {
      options.standard_only = true;
    } else if (option == "--report-only") {
      options.report_only = true;
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
  if (options.deterministic_repetitions == 0) {
    UsageError("--deterministic-repetitions must be positive");
  }
  if (options.minimalloc_timeout_ms == 0) {
    UsageError("--minimalloc-timeout-ms must be positive");
  }
  if (options.standard_only && options.standard_capacity) {
    UsageError("--standard-only cannot be combined with --standard-capacity");
  }
  if (options.standard_only && !options.build_core_relaxations && !options.pypto_roots.empty()) {
    UsageError("--standard-only requires core projections for --pypto inputs");
  }
  if (options.report_only && !options.standard_only) {
    UsageError("--report-only currently requires --standard-only");
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

std::string MetadataValue(const dsa::StructuredProblemDocument& document, const std::string& key) {
  const auto found = document.metadata.find(key);
  return found == document.metadata.end() ? std::string{} : found->second;
}

std::string MemorySpaceName(std::string_view pool_name) {
  if (pool_name == "Vec") return "UB";
  if (pool_name == "Mat") return "L1";
  if (pool_name == "Left") return "L0A";
  if (pool_name == "Right") return "L0B";
  if (pool_name == "Acc") return "L0C";
  return std::string(pool_name);
}

bool ShareAllowedPool(const dsa::Buffer& first, const dsa::Buffer& second) {
  return std::any_of(first.allowed_pools.begin(), first.allowed_pools.end(), [&](dsa::PoolId pool) {
    return std::find(second.allowed_pools.begin(), second.allowed_pools.end(), pool) !=
           second.allowed_pools.end();
  });
}

std::map<dsa::PoolId, std::uint64_t> MaxLiveBytesByPool(const dsa::DsaProblem& problem,
                                                        bool* complete) {
  struct EventTotals {
    std::uint64_t ending = 0;
    std::uint64_t starting = 0;
  };
  std::map<dsa::PoolId, std::map<std::int64_t, EventTotals>> events_by_pool;
  *complete = true;
  for (const dsa::Buffer& buffer : problem.buffers) {
    if (buffer.allowed_pools.size() != 1) {
      *complete = false;
      continue;
    }
    std::vector<dsa::Interval> intervals = buffer.live_intervals;
    std::sort(intervals.begin(), intervals.end(), [](const auto& first, const auto& second) {
      return std::tie(first.lower, first.upper) < std::tie(second.lower, second.upper);
    });
    std::vector<dsa::Interval> merged;
    for (const dsa::Interval& interval : intervals) {
      if (merged.empty() || interval.lower > merged.back().upper) {
        merged.push_back(interval);
      } else {
        merged.back().upper = std::max(merged.back().upper, interval.upper);
      }
    }
    for (const dsa::Interval& interval : merged) {
      EventTotals& start = events_by_pool[buffer.allowed_pools.front()][interval.lower];
      EventTotals& end = events_by_pool[buffer.allowed_pools.front()][interval.upper];
      if (start.starting > std::numeric_limits<std::uint64_t>::max() - buffer.size ||
          end.ending > std::numeric_limits<std::uint64_t>::max() - buffer.size) {
        throw std::overflow_error("live-byte event total overflow");
      }
      start.starting += buffer.size;
      end.ending += buffer.size;
    }
  }

  std::map<dsa::PoolId, std::uint64_t> result;
  for (const auto& [pool, events] : events_by_pool) {
    std::uint64_t current = 0;
    std::uint64_t peak = 0;
    for (const auto& [time, totals] : events) {
      static_cast<void>(time);
      if (totals.ending > current) {
        throw std::runtime_error("live-byte event sweep underflow");
      }
      current -= totals.ending;
      if (totals.starting > std::numeric_limits<std::uint64_t>::max() - current) {
        throw std::overflow_error("live-byte event sweep overflow");
      }
      current += totals.starting;
      peak = std::max(peak, current);
    }
    result.emplace(pool, peak);
  }
  return result;
}

FeatureStats AnalyzeFeatures(const Instance& instance) {
  FeatureStats stats;
  stats.instance = instance.document.instance;
  stats.profile = dsa::ToString(instance.document.profile);
  stats.source = instance.source;
  stats.corpus_namespace = MetadataValue(instance.document, "corpus_namespace");
  stats.corpus_family = MetadataValue(instance.document, "corpus_family");
  stats.corpus_source_path = MetadataValue(instance.document, "corpus_source_path");
  stats.target = MetadataValue(instance.document, "target");
  const dsa::DsaProblem& problem = instance.document.problem;
  stats.buffers = problem.buffers.size();
  stats.pools = problem.pools.size();
  std::set<std::uint64_t> distinct_sizes;
  stats.min_buffer_bytes = std::numeric_limits<std::uint64_t>::max();
  stats.min_alignment = std::numeric_limits<std::uint64_t>::max();
  for (const dsa::Buffer& buffer : problem.buffers) {
    stats.min_buffer_bytes = std::min(stats.min_buffer_bytes, buffer.size);
    stats.max_buffer_bytes = std::max(stats.max_buffer_bytes, buffer.size);
    if (stats.total_buffer_bytes > std::numeric_limits<std::uint64_t>::max() - buffer.size) {
      throw std::overflow_error("total buffer bytes overflow");
    }
    stats.total_buffer_bytes += buffer.size;
    distinct_sizes.insert(buffer.size);
    stats.min_alignment = std::min(stats.min_alignment, buffer.alignment);
    stats.max_alignment = std::max(stats.max_alignment, buffer.alignment);
    if (buffer.alignment > 1) ++stats.aligned_buffers;
    if (buffer.live_intervals.size() > 1) ++stats.multi_interval_buffers;
    if (buffer.allowed_pools.size() > 1) ++stats.flexible_pool_buffers;
  }
  if (problem.buffers.empty()) {
    stats.min_buffer_bytes = 0;
    stats.min_alignment = 0;
  }
  stats.distinct_buffer_sizes = distinct_sizes.size();
  stats.uniform_buffer_size = distinct_sizes.size() <= 1;

  for (std::size_t first = 0; first < problem.buffers.size(); ++first) {
    for (std::size_t second = first + 1; second < problem.buffers.size(); ++second) {
      if (!ShareAllowedPool(problem.buffers[first], problem.buffers[second])) continue;
      if (dsa::HaveTemporalConflict(problem, problem.buffers[first], problem.buffers[second])) {
        ++stats.temporal_conflicts;
      } else {
        ++stats.reuse_candidates;
      }
    }
  }

  const std::map<dsa::PoolId, std::uint64_t> max_live_bytes =
      MaxLiveBytesByPool(problem, &stats.live_byte_stats_complete);
  for (const dsa::Pool& pool : problem.pools) {
    const std::string space = MemorySpaceName(pool.name);
    stats.memory_spaces.push_back(space);
    stats.pool_capacities.push_back(space + "=" +
                                    (pool.capacity ? std::to_string(*pool.capacity) : "unbounded"));
    const auto live = max_live_bytes.find(pool.id);
    const std::uint64_t live_bytes = live == max_live_bytes.end() ? 0 : live->second;
    stats.max_live_bytes_by_space.push_back(space + "=" + std::to_string(live_bytes));
    if (stats.live_byte_stats_complete && pool.capacity && *pool.capacity != 0) {
      const long double ratio =
          static_cast<long double>(live_bytes) / static_cast<long double>(*pool.capacity);
      if (!stats.max_live_capacity_ratio || ratio > *stats.max_live_capacity_ratio) {
        stats.max_live_capacity_ratio = ratio;
        stats.tightest_space = space;
      }
    }
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
    stats.alias_classes = problem.pypto_structure->alias_classes.size();
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

std::string CanonicalStandardShape(const dsa::DsaProblem& problem) {
  std::set<std::int64_t> event_values;
  for (const dsa::Buffer& buffer : problem.buffers) {
    if (buffer.live_intervals.size() != 1) {
      throw std::logic_error("standard shape fingerprint requires one interval per buffer");
    }
    event_values.insert(buffer.live_intervals.front().lower);
    event_values.insert(buffer.live_intervals.front().upper);
  }
  std::map<std::int64_t, std::size_t> event_rank;
  std::size_t next_rank = 0;
  for (const std::int64_t event : event_values) event_rank.emplace(event, next_rank++);

  using BufferShape = std::tuple<std::uint64_t, std::size_t, std::size_t>;
  std::vector<BufferShape> buffers;
  buffers.reserve(problem.buffers.size());
  for (const dsa::Buffer& buffer : problem.buffers) {
    const dsa::Interval& interval = buffer.live_intervals.front();
    buffers.emplace_back(buffer.size, event_rank.at(interval.lower), event_rank.at(interval.upper));
  }
  std::sort(buffers.begin(), buffers.end());
  std::ostringstream output;
  for (const auto& [size, lower, upper] : buffers) {
    output << size << ':' << lower << ':' << upper << ';';
  }
  return output.str();
}

bool HasNontrivialStandardChoice(const dsa::DsaProblem& problem) {
  bool has_conflict = false;
  bool has_reuse_candidate = false;
  for (std::size_t first = 0; first < problem.buffers.size(); ++first) {
    for (std::size_t second = first + 1; second < problem.buffers.size(); ++second) {
      if (dsa::LifetimesOverlap(problem.buffers[first], problem.buffers[second])) {
        has_conflict = true;
      } else {
        has_reuse_candidate = true;
      }
    }
  }
  return has_conflict && has_reuse_candidate;
}

dsa::StructuredProblemDocument MakeStandardProjection(dsa::StructuredProblemDocument relaxation) {
  const std::string source_instance = relaxation.relaxed_from.value_or(relaxation.instance);
  const std::string pool_name = MetadataValue(relaxation, "source_pool_name");
  std::ostringstream removed_features;
  for (std::size_t index = 0; index < relaxation.relaxed_features.size(); ++index) {
    if (index != 0) removed_features << ',';
    removed_features << relaxation.relaxed_features[index];
  }

  relaxation.profile = dsa::BenchmarkProfile::kStandardDsa;
  relaxation.instance = source_instance + "::" + (pool_name.empty() ? "pool" : pool_name);
  relaxation.metadata["standard_origin"] = "pypto_projection";
  relaxation.metadata["projected_from"] = source_instance;
  relaxation.metadata["projection_relaxed_features"] = removed_features.str();
  relaxation.relaxed_from.reset();
  relaxation.relaxed_features.clear();
  for (dsa::Pool& pool : relaxation.problem.pools) pool.capacity.reset();

  const std::vector<std::string> errors = dsa::ValidateStructuredProblemDocument(relaxation);
  if (!errors.empty()) {
    throw std::logic_error("generated an invalid standard projection: " + errors.front());
  }
  return relaxation;
}

std::vector<Instance> LoadInstances(const Options& options) {
  std::vector<Instance> instances;
  std::set<std::string> standard_shapes;
  std::set<std::string> compiler_problem_fingerprints;
  std::size_t duplicate_compiler_problems = 0;
  std::size_t duplicate_standard_projections = 0;
  std::size_t trivial_standard_projections = 0;
  for (const fs::path& path : DiscoverFiles(options.standard_roots, {".csv", ".json"})) {
    Instance standard = LoadStandardInstance(path, options.standard_capacity);
    if (options.standard_only) {
      for (dsa::Pool& pool : standard.document.problem.pools) pool.capacity.reset();
      standard.document.metadata["standard_origin"] = "public_standard";
      standard_shapes.insert(CanonicalStandardShape(standard.document.problem));
    }
    instances.push_back(std::move(standard));
  }
  for (const fs::path& path : DiscoverFiles(options.pypto_roots, {".json"})) {
    Instance structured = LoadPyptoInstance(path);
    const std::string fingerprint =
        MetadataValue(structured.document, "corpus_problem_fingerprint_fnv1a64");
    if (!fingerprint.empty() && !compiler_problem_fingerprints.insert(fingerprint).second) {
      ++duplicate_compiler_problems;
      continue;
    }
    if (!options.standard_only) instances.push_back(structured);
    if (!options.build_core_relaxations) continue;
    try {
      for (dsa::StructuredProblemDocument& relaxation :
           dsa::BuildCoreRelaxations(structured.document)) {
        if (!options.standard_only) {
          instances.push_back({std::move(relaxation), structured.source});
          continue;
        }
        dsa::StructuredProblemDocument projection = MakeStandardProjection(std::move(relaxation));
        if (!HasNontrivialStandardChoice(projection.problem)) {
          ++trivial_standard_projections;
          continue;
        }
        if (!standard_shapes.insert(CanonicalStandardShape(projection.problem)).second) {
          ++duplicate_standard_projections;
          continue;
        }
        instances.push_back({std::move(projection), structured.source});
      }
    } catch (const std::exception& error) {
      std::cerr << "dsa-suite: lower bounds unavailable for " << structured.document.instance
                << ": " << error.what() << '\n';
    }
  }
  if (duplicate_compiler_problems != 0) {
    std::cerr << "dsa-suite: skipped " << duplicate_compiler_problems
              << " repeated compiler problem shapes across corpus inputs\n";
  }
  if (options.standard_only) {
    std::cerr << "dsa-suite: standard projection retained " << instances.size()
              << " instances; skipped " << trivial_standard_projections << " trivial and "
              << duplicate_standard_projections << " duplicate PyPTO pool projections\n";
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
  } else if (method == kCanonicalGreedy) {
    solver = std::make_unique<dsa::CanonicalGreedySolver>();
  } else if (method == kPromoteRepair) {
    solver = std::make_unique<dsa::PromoteRepairSolver>();
  } else if (method == kCypressRelaxation) {
    solver = std::make_unique<dsa::CypressRelaxationSolver>();
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
  record.solver_metrics = result.solver_metrics;
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
    const std::size_t deterministic_runs =
        options.standard_only ? options.deterministic_repetitions : 1;
    const bool has_fixed_capacity =
        std::all_of(instance.document.problem.pools.begin(), instance.document.problem.pools.end(),
                    [](const dsa::Pool& pool) { return pool.capacity.has_value(); });
    for (std::size_t repetition = 0; repetition < deterministic_runs; ++repetition) {
      records.push_back(RunHeuristic(instance, kFirstFit, std::nullopt, options));
      if (has_fixed_capacity) {
        records.push_back(RunHeuristic(instance, kCanonicalGreedy, std::nullopt, options));
        records.push_back(RunHeuristic(instance, kPromoteRepair, std::nullopt, options));
        records.push_back(RunHeuristic(instance, kCypressRelaxation, std::nullopt, options));
      }
      records.push_back(RunHeuristic(instance, kXlaHeap, std::nullopt, options));
    }
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
  json["solver_metrics"] = record.solver_metrics;
  json["objective_score"] = record.objective_score;
  json["metadata"] = record.metadata;
  json["relaxed_from"] = record.relaxed_from ? Json(*record.relaxed_from) : Json(nullptr);
  return json;
}

template <typename T>
std::optional<T> OptionalJsonValue(const Json& json, std::string_view key) {
  const auto value = json.find(std::string(key));
  if (value == json.end() || value->is_null()) return std::nullopt;
  return value->get<T>();
}

RunRecord RunRecordFromJson(const Json& json) {
  RunRecord record;
  record.instance = json.at("instance").get<std::string>();
  record.profile = json.at("profile").get<std::string>();
  record.source = json.at("source").get<std::string>();
  record.method = json.at("method").get<std::string>();
  record.comparison_scope = json.at("comparison_scope").get<std::string>();
  record.status = json.at("status").get<std::string>();
  record.relaxed_from = OptionalJsonValue<std::string>(json, "relaxed_from");
  record.seed = OptionalJsonValue<std::uint64_t>(json, "seed");
  record.capacity = OptionalJsonValue<std::uint64_t>(json, "capacity");
  record.peak = OptionalJsonValue<std::uint64_t>(json, "peak");
  record.total_peak = OptionalJsonValue<std::uint64_t>(json, "total_peak");
  record.capacity_overflow = OptionalJsonValue<std::uint64_t>(json, "capacity_overflow");
  record.reuse_cost = OptionalJsonValue<std::uint64_t>(json, "reuse_cost");
  record.bank_cost = OptionalJsonValue<std::uint64_t>(json, "bank_cost");
  record.backtracks = OptionalJsonValue<std::uint64_t>(json, "backtracks");
  record.runtime_us = json.at("runtime_us").get<std::uint64_t>();
  record.buffers = json.at("buffers").get<std::size_t>();
  record.placement_valid = json.at("placement_valid").get<bool>();
  record.solution_valid = json.at("solution_valid").get<bool>();
  record.structurally_compatible = json.at("structurally_compatible").get<bool>();
  record.objective_compatible = json.at("objective_compatible").get<bool>();
  record.certified_optimal = json.at("certified_optimal").get<bool>();
  record.target = OptionalJsonValue<std::string>(json, "target").value_or("");
  record.features = json.at("features").get<std::vector<std::string>>();
  record.relaxed_features = json.at("relaxed_features").get<std::vector<std::string>>();
  record.diagnostics = json.at("diagnostics").get<std::vector<std::string>>();
  if (json.contains("solver_metrics")) {
    record.solver_metrics = json.at("solver_metrics").get<std::map<std::string, std::uint64_t>>();
  }
  record.objective_score = json.at("objective_score").get<std::vector<std::uint64_t>>();
  record.metadata = json.at("metadata").get<std::map<std::string, std::string>>();
  return record;
}

std::vector<RunRecord> ReadRawResults(const fs::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read raw results: " + path.string());
  std::vector<RunRecord> records;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) continue;
    try {
      records.push_back(RunRecordFromJson(Json::parse(line)));
    } catch (const std::exception& error) {
      throw std::runtime_error("invalid raw result at " + path.string() + ':' +
                               std::to_string(line_number) + ": " + error.what());
    }
  }
  if (records.empty()) throw std::runtime_error("raw results are empty: " + path.string());
  return records;
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

std::string FormatRatio(long double value) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(6) << value;
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
  output << "instance,profile,source,corpus_namespace,corpus_family,corpus_source_path,target,"
            "buffers,pools,memory_spaces,pool_capacities_bytes,min_buffer_bytes,max_buffer_bytes,"
            "total_buffer_bytes,distinct_buffer_sizes,uniform_buffer_size,min_alignment,"
            "max_alignment,temporal_conflicts,reuse_candidates,conflict_density,"
            "max_interval_live_bytes_by_space,live_byte_stats_complete,tightest_space,"
            "max_live_capacity_ratio,aligned_buffers,multi_interval_buffers,"
            "flexible_pool_buffers,reserved_ranges,bank_geometries,colocations,separations,"
            "pipeline_separations,target_hazard_separations,semantic_no_alias_separations,"
            "temporal_exclusions,pinned_allocations,reuse_penalties,"
            "alias_classes,nontrivial_alias_classes,pipeline_groups,depth_shed_pipeline_groups\n";
  std::vector<FeatureStats> rows;
  for (const Instance& instance : instances) {
    if (instance.document.profile == dsa::BenchmarkProfile::kPyptoCoreRelaxation) continue;
    rows.push_back(AnalyzeFeatures(instance));
  }
  std::sort(rows.begin(), rows.end(), [](const FeatureStats& first, const FeatureStats& second) {
    return std::tie(first.instance, first.profile, first.source) <
           std::tie(second.instance, second.profile, second.source);
  });
  for (const FeatureStats& stats : rows) {
    const std::size_t comparable_pairs = stats.temporal_conflicts + stats.reuse_candidates;
    const std::string conflict_density =
        comparable_pairs == 0 ? std::string{}
                              : FormatRatio(static_cast<long double>(stats.temporal_conflicts) /
                                            static_cast<long double>(comparable_pairs));
    output << CsvEscape(stats.instance) << ',' << CsvEscape(stats.profile) << ','
           << CsvEscape(stats.source) << ',' << CsvEscape(stats.corpus_namespace) << ','
           << CsvEscape(stats.corpus_family) << ',' << CsvEscape(stats.corpus_source_path) << ','
           << CsvEscape(stats.target) << ',' << stats.buffers << ',' << stats.pools << ','
           << CsvEscape(Join(stats.memory_spaces, ";")) << ','
           << CsvEscape(Join(stats.pool_capacities, ";")) << ',' << stats.min_buffer_bytes << ','
           << stats.max_buffer_bytes << ',' << stats.total_buffer_bytes << ','
           << stats.distinct_buffer_sizes << ',' << (stats.uniform_buffer_size ? "true" : "false")
           << ',' << stats.min_alignment << ',' << stats.max_alignment << ','
           << stats.temporal_conflicts << ',' << stats.reuse_candidates << ',' << conflict_density
           << ',' << CsvEscape(Join(stats.max_live_bytes_by_space, ";")) << ','
           << (stats.live_byte_stats_complete ? "true" : "false") << ','
           << CsvEscape(stats.tightest_space) << ','
           << (stats.max_live_capacity_ratio ? FormatRatio(*stats.max_live_capacity_ratio)
                                             : std::string{})
           << ',' << stats.aligned_buffers << ',' << stats.multi_interval_buffers << ','
           << stats.flexible_pool_buffers << ',' << stats.reserved_ranges << ','
           << stats.bank_geometries << ',' << stats.colocations << ',' << stats.separations << ','
           << stats.pipeline_separations << ',' << stats.target_hazard_separations << ','
           << stats.semantic_no_alias_separations << ',' << stats.temporal_exclusions << ','
           << stats.pinned_allocations << ',' << stats.reuse_penalties << ',' << stats.alias_classes
           << ',' << stats.nontrivial_alias_classes << ',' << stats.pipeline_groups << ','
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

bool HasPrefix(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string StandardBenchmarkFamily(const dsa::StructuredProblemDocument& document) {
  if (MetadataValue(document, "standard_origin") != "pypto_projection") {
    return "MiniMalloc benchmark corpus";
  }
  constexpr std::string_view kPyptoLibPrefix = "pypto-lib::";
  return HasPrefix(document.instance, kPyptoLibPrefix) ? "PyPTO-Lib" : "PyPTO";
}

struct StandardAggregateCell {
  std::size_t quality_instances = 0;
  std::size_t wins = 0;
  long double log_peak_ratio_sum = 0.0L;
  std::size_t runtime_instances = 0;
  long double log_runtime_ratio_sum = 0.0L;
};

struct StandardAggregateGroup {
  std::string name;
  std::size_t instances = 0;
  std::size_t proven_optima = 0;
  std::map<std::string, StandardAggregateCell> by_method;
};

void AccumulateStandardInstance(
    StandardAggregateGroup* group, const Instance& instance, const std::vector<Summary>& summaries,
    const std::vector<std::pair<std::string_view, std::string_view>>& methods) {
  ++group->instances;
  const std::string profile = dsa::ToString(instance.document.profile);
  const Summary* exact = FindSummary(summaries, instance.document.instance, profile, kMiniMalloc);
  if (exact != nullptr && exact->certified_optimal) ++group->proven_optima;

  std::optional<std::uint64_t> reference_peak;
  for (const auto& [label, method] : methods) {
    static_cast<void>(label);
    const Summary* summary = FindSummary(summaries, instance.document.instance, profile, method);
    if (summary == nullptr || !summary->best || !HasUsableSolution(*summary->best)) continue;
    if (!reference_peak || *summary->best->peak < *reference_peak) {
      reference_peak = summary->best->peak;
    }
  }

  const Summary* first_fit = FindSummary(summaries, instance.document.instance, profile, kFirstFit);
  const std::optional<std::uint64_t> runtime_reference =
      first_fit != nullptr && first_fit->best && first_fit->median_runtime_us != 0 &&
              first_fit->best->status != "not_run" && first_fit->best->status != "unavailable"
          ? std::optional<std::uint64_t>(first_fit->median_runtime_us)
          : std::nullopt;

  for (const auto& [label, method] : methods) {
    static_cast<void>(label);
    const Summary* summary = FindSummary(summaries, instance.document.instance, profile, method);
    if (summary == nullptr || !summary->best) continue;
    StandardAggregateCell& cell = group->by_method[std::string(method)];
    if (reference_peak && *reference_peak != 0 && HasUsableSolution(*summary->best)) {
      const long double ratio = static_cast<long double>(*summary->best->peak) /
                                static_cast<long double>(*reference_peak);
      cell.log_peak_ratio_sum += std::log(ratio);
      ++cell.quality_instances;
      if (*summary->best->peak == *reference_peak) ++cell.wins;
    }
    if (runtime_reference && summary->median_runtime_us != 0 &&
        summary->best->status != "not_run" && summary->best->status != "unavailable") {
      const long double runtime_ratio = static_cast<long double>(summary->median_runtime_us) /
                                        static_cast<long double>(*runtime_reference);
      cell.log_runtime_ratio_sum += std::log(runtime_ratio);
      ++cell.runtime_instances;
    }
  }
}

std::string FormatAggregateRatio(const StandardAggregateCell* cell) {
  if (cell == nullptr || cell->quality_instances == 0) return "—";
  const long double geometric_mean =
      std::exp(cell->log_peak_ratio_sum / static_cast<long double>(cell->quality_instances));
  std::ostringstream output;
  output << std::fixed << std::setprecision(5) << geometric_mean;
  return output.str();
}

std::string FormatAggregateQuality(const StandardAggregateCell* cell) {
  if (cell == nullptr || cell->quality_instances == 0) return "—";
  return FormatAggregateRatio(cell) + " (" + std::to_string(cell->wins) + '/' +
         std::to_string(cell->quality_instances) + ')';
}

std::string FormatAggregateRuntimeRatio(const StandardAggregateCell* cell) {
  if (cell == nullptr || cell->runtime_instances == 0) return "—";
  const long double geometric_mean =
      std::exp(cell->log_runtime_ratio_sum / static_cast<long double>(cell->runtime_instances));
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << geometric_mean << "x";
  return output.str();
}

void WriteStandardOnlyReport(const fs::path& path, const std::vector<Instance>& instances,
                             const std::vector<Summary>& summaries, const Options& options) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot write Markdown report: " + path.string());
  output << "# Standard DSA benchmark results\n\n"
         << "This presentation aggregates capacity-free, single-pool standard DSA problems by "
            "source corpus. Public MiniMalloc "
            "instances are used directly. PyPTO rows are per-pool projections that retain buffer "
            "sizes and lifetimes but remove compiler-specific constraints, alignment, capacity, "
            "and architecture resources; they are not device-valid PyPTO placements.\n\n"
         << "Full per-instance results remain in `summary.csv`; raw repetitions are in "
            "`results.jsonl`. The tables below are the presentation layer.\n\n"
         << "Configuration: run label `" << MarkdownEscape(options.run_label) << "`; seeds `";
  for (std::size_t index = 0; index < options.seeds.size(); ++index) {
    if (index != 0) output << ',';
    output << options.seeds[index];
  }
  output << "`; search budget `" << options.iterations << "`; local-search restarts `"
         << options.restarts << "`; deterministic repetitions `"
         << options.deterministic_repetitions << "`; MiniMalloc timeout `"
         << options.minimalloc_timeout_ms
         << " ms` per instance. Peak is the best valid result. "
            "Runtime is the median across deterministic repetitions or stochastic seeds; "
            "MiniMalloc is one bounded run. All returned placements are independently "
            "validated.\n\n"
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
  std::ostringstream seeds;
  for (std::size_t index = 0; index < options.seeds.size(); ++index) {
    if (index != 0) seeds << ',';
    seeds << options.seeds[index];
  }
  append_option("seeds", seeds.str());
  append_option("iterations", std::to_string(options.iterations));
  append_option("restarts", std::to_string(options.restarts));
  append_option("stagnation", std::to_string(options.stagnation_limit));
  append_option("deterministic-repetitions", std::to_string(options.deterministic_repetitions));
  append_option("minimalloc-timeout-ms", std::to_string(options.minimalloc_timeout_ms));
  output << " \\" << '\n' << "  --standard-only";
  if (!options.run_minimalloc) output << " \\" << '\n' << "  --no-minimalloc";
  output << "\n```\n\n";

  const std::vector<std::pair<std::string_view, std::string_view>> methods = {
      {"MiniMalloc exact", kMiniMalloc}, {"First fit", kFirstFit},       {"XLA heap", kXlaHeap},
      {"TVM hill climb", kTvmHillClimb}, {"Local search", kLocalSearch},
  };
  std::map<std::string, StandardAggregateGroup> groups;
  for (const Instance& instance : instances) {
    const std::string family = StandardBenchmarkFamily(instance.document);
    StandardAggregateGroup& group = groups[family];
    group.name = family;
    AccumulateStandardInstance(&group, instance, summaries, methods);
  }

  auto write_quality_header = [&]() {
    output << "| Instance corpus | N";
    for (const auto& [label, method] : methods) {
      static_cast<void>(method);
      output << " | " << label;
    }
    output << " |\n| --- | ---:";
    for (std::size_t index = 0; index < methods.size(); ++index) output << " | ---:";
    output << " |\n";
  };
  auto write_runtime_header = [&]() {
    output << "| Instance corpus | N";
    for (const auto& [label, method] : methods) {
      static_cast<void>(method);
      output << " | " << label;
    }
    output << " |\n| --- | ---:";
    for (std::size_t index = 0; index < methods.size(); ++index) output << " | ---:";
    output << " |\n";
  };

  auto write_quality_row = [&](const StandardAggregateGroup& group) {
    output << "| " << MarkdownEscape(group.name) << " | " << group.instances;
    for (const auto& [label, method] : methods) {
      static_cast<void>(label);
      const auto cell = group.by_method.find(std::string(method));
      output << " | "
             << FormatAggregateQuality(cell == group.by_method.end() ? nullptr : &cell->second);
    }
    output << " |\n";
  };

  const auto minimalloc = groups.find("MiniMalloc benchmark corpus");
  const auto pypto = groups.find("PyPTO");
  const auto pypto_lib = groups.find("PyPTO-Lib");
  if (minimalloc != groups.end() && pypto != groups.end() && pypto_lib != groups.end()) {
    const StandardAggregateGroup& minimalloc_group = minimalloc->second;
    const StandardAggregateGroup& pypto_group = pypto->second;
    const StandardAggregateGroup& pypto_lib_group = pypto_lib->second;
    const StandardAggregateCell& pypto_tvm = pypto_group.by_method.at(std::string(kTvmHillClimb));
    const StandardAggregateCell& pypto_local = pypto_group.by_method.at(std::string(kLocalSearch));
    const StandardAggregateCell& pypto_lib_tvm =
        pypto_lib_group.by_method.at(std::string(kTvmHillClimb));
    const StandardAggregateCell& pypto_lib_local =
        pypto_lib_group.by_method.at(std::string(kLocalSearch));
    output << "## Highlights\n\n"
           << "- MiniMalloc certified all " << pypto_group.proven_optima << '/'
           << pypto_group.instances << " PyPTO and " << pypto_lib_group.proven_optima << '/'
           << pypto_lib_group.instances << " PyPTO-Lib reference peaks, but only "
           << minimalloc_group.proven_optima << '/' << minimalloc_group.instances
           << " public MiniMalloc cases under the bounded timeout.\n"
           << "- TVM hill climb and local search both match the reference on all " << pypto_tvm.wins
           << '/' << pypto_tvm.quality_instances << " PyPTO and " << pypto_lib_tvm.wins << '/'
           << pypto_lib_tvm.quality_instances
           << " PyPTO-Lib instances (local-search wins: " << pypto_local.wins << '/'
           << pypto_local.quality_instances << " and " << pypto_lib_local.wins << '/'
           << pypto_lib_local.quality_instances << ").\n"
           << "- The public MiniMalloc family remains discriminating: TVM hill climb has a "
              "geometric-mean peak ratio of "
           << FormatAggregateRatio(&minimalloc_group.by_method.at(std::string(kTvmHillClimb)))
           << ", versus "
           << FormatAggregateRatio(&minimalloc_group.by_method.at(std::string(kLocalSearch)))
           << " for local search.\n\n";
  }

  output << "## Solution quality\n\n"
         << "Each solver cell is `geometric-mean peak / reference peak (wins/N)`. The reference "
            "is the lowest independently validated peak found for that instance. The highlights "
            "state where MiniMalloc certified it as optimal. Lower is better, `1.000` is ideal, "
            "and a win includes ties.\n\n";
  write_quality_header();
  for (const auto& [name, group] : groups) {
    static_cast<void>(name);
    write_quality_row(group);
  }

  output << "\n## Runtime relative to first fit\n\n";
  output << "Each cell is the geometric mean of the per-instance `solver median / first-fit "
            "median` ratio. Normalizing before aggregation controls for instance size and "
            "difficulty; `1.00x` is first-fit speed. First fit and XLA use the median of "
         << options.deterministic_repetitions
         << " repetitions per instance; stochastic searches use the median of "
         << options.seeds.size()
         << " seeds; MiniMalloc is one bounded run. Runtime is machine-dependent.\n\n";
  write_runtime_header();
  auto write_runtime_row = [&](const StandardAggregateGroup& group) {
    output << "| " << MarkdownEscape(group.name) << " | " << group.instances;
    for (const auto& [label, method] : methods) {
      static_cast<void>(label);
      const auto cell = group.by_method.find(std::string(method));
      output << " | "
             << FormatAggregateRuntimeRatio(cell == group.by_method.end() ? nullptr
                                                                          : &cell->second);
    }
    output << " |\n";
  };
  for (const auto& [name, group] : groups) {
    static_cast<void>(name);
    write_runtime_row(group);
  }
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

  output
      << "\n## PyPTO structured DSA\n\n"
      << "| Instance | Profile | Family | Source | Target | Buffers | Structure | Exact core "
         "lower bound | "
         "First fit | Canonical greedy | Promote-repair | Cypress relaxation | XLA heap | TVM hill "
         "climb | Local search | PyPTO structured search |\n"
      << "| --- | --- | --- | --- | --- | ---: | --- | --- | --- | --- | --- | --- | --- | --- | "
         "--- | --- |\n";
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
           << HeuristicCell(FindSummary(summaries, document.instance, profile, kCanonicalGreedy),
                            nullptr, true)
           << " | "
           << HeuristicCell(FindSummary(summaries, document.instance, profile, kPromoteRepair),
                            nullptr, true)
           << " | "
           << HeuristicCell(FindSummary(summaries, document.instance, profile, kCypressRelaxation),
                            nullptr, true)
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
  const fs::path report_path = options.output_dir / "report.md";
  WriteRawResults(raw_path, records, options);
  WriteSummaryCsv(csv_path, summaries);
  if (options.standard_only) {
    WriteStandardOnlyReport(report_path, instances, summaries, options);
    std::cout << "dsa-suite: wrote " << raw_path << ", " << csv_path << ", and " << report_path
              << '\n';
  } else {
    const fs::path features_path = options.output_dir / "features.csv";
    WriteFeaturesCsv(features_path, instances);
    WriteReport(report_path, instances, summaries, options);
    std::cout << "dsa-suite: wrote " << raw_path << ", " << csv_path << ", " << features_path
              << ", and " << report_path << '\n';
  }
}

void RewriteStandardReport(const Options& options, const std::vector<Instance>& instances) {
  const fs::path raw_path = options.output_dir / "results.jsonl";
  const std::vector<RunRecord> records = ReadRawResults(raw_path);
  std::set<std::pair<std::string, std::string>> expected;
  for (const Instance& instance : instances) {
    expected.emplace(instance.document.instance, dsa::ToString(instance.document.profile));
  }
  std::set<std::pair<std::string, std::string>> actual;
  for (const RunRecord& record : records) {
    if (record.capacity) {
      throw std::runtime_error("--report-only found a capacity-bearing result for " +
                               record.instance);
    }
    actual.emplace(record.instance, record.profile);
  }
  if (actual != expected) {
    throw std::runtime_error("--report-only inputs do not match the instance set in results.jsonl");
  }
  const fs::path report_path = options.output_dir / "report.md";
  WriteStandardOnlyReport(report_path, instances, Summarize(records), options);
  std::cout << "dsa-suite: rebuilt " << report_path << " from " << raw_path << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = ParseOptions(argc, argv);
    const std::vector<Instance> instances = LoadInstances(options);
    std::cout << "dsa-suite: loaded " << instances.size() << " benchmark documents" << std::endl;
    if (options.report_only) {
      RewriteStandardReport(options, instances);
      return 0;
    }
    const std::vector<RunRecord> records = ExecuteSuite(instances, options);
    WriteOutputs(options, instances, records);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "dsa-suite: " << error.what() << '\n';
    return 1;
  }
}

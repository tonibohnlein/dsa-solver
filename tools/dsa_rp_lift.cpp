// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "dsa/algorithms/first_fit_solver.h"
#include "dsa/analysis/penalty_edge_derivation.h"
#include "dsa/io/minimalloc_csv.h"
#include "dsa/model/model.h"
#include "dsa/model/structured_problem.h"

namespace {

namespace fs = std::filesystem;

struct Options {
  std::vector<fs::path> inputs;
  fs::path output;
  std::uint32_t streams = 4;
  std::uint64_t seed = 0;
  std::optional<std::uint64_t> capacity;
  bool capacity_from_first_fit = false;
  std::string source_commit;
};

[[noreturn]] void UsageError(const std::string& message) {
  throw std::invalid_argument(message + "\nRun dsa-rp-lift --help for usage information.");
}

std::uint64_t ParseUnsigned(std::string_view text, const std::string& option) {
  if (text.empty() || text.front() == '-') {
    UsageError(option + " requires a non-negative integer");
  }
  std::uint64_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    UsageError(option + " requires a non-negative integer");
  }
  return value;
}

void PrintHelp() {
  std::cout << "Usage: dsa-rp-lift --input PATH... --output DIR --source-commit SHA [options]\n\n"
            << "Lift MiniMalloc CSV geometry into synthetic scheduled-program DSA-RP instances.\n"
            << "Each buffer has one first write and one final read. Weighted soft edges are\n"
            << "derived mechanically by the maximal-access happens-before rule.\n\n"
            << "Required:\n"
            << "  --input PATH           CSV file or directory (repeatable)\n"
            << "  --output DIR           Output directory for flat JSON instances\n"
            << "  --source-commit SHA    Exact MiniMalloc source revision\n\n"
            << "Options:\n"
            << "  --streams N            Completion-ordered streams (default: 4)\n"
            << "  --seed N               Deterministic stream-assignment seed (default: 0)\n"
            << "  --capacity BYTES       Override capacity; otherwise parse FILE.*.csv\n"
            << "  --capacity-first-fit   Use the deterministic standard first-fit peak\n"
            << "  --help                  Show this help\n";
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
    } else if (option == "--input") {
      options.inputs.emplace_back(next(&index, option));
    } else if (option == "--output") {
      options.output = fs::path(next(&index, option));
    } else if (option == "--source-commit") {
      options.source_commit = next(&index, option);
    } else if (option == "--streams") {
      const std::uint64_t value = ParseUnsigned(next(&index, option), option);
      if (value == 0 || value > std::numeric_limits<std::uint32_t>::max()) {
        UsageError("--streams must be in [1, UINT32_MAX]");
      }
      options.streams = static_cast<std::uint32_t>(value);
    } else if (option == "--seed") {
      options.seed = ParseUnsigned(next(&index, option), option);
    } else if (option == "--capacity") {
      options.capacity = ParseUnsigned(next(&index, option), option);
      if (*options.capacity == 0) UsageError("--capacity must be positive");
    } else if (option == "--capacity-first-fit") {
      options.capacity_from_first_fit = true;
    } else {
      UsageError("unknown option '" + option + "'");
    }
  }
  if (options.inputs.empty()) UsageError("at least one --input is required");
  if (options.output.empty()) UsageError("--output is required");
  if (options.source_commit.empty()) UsageError("--source-commit is required");
  if (options.capacity && options.capacity_from_first_fit) {
    UsageError("--capacity and --capacity-first-fit are mutually exclusive");
  }
  return options;
}

std::vector<fs::path> DiscoverCsvFiles(const std::vector<fs::path>& roots) {
  std::vector<fs::path> files;
  for (const fs::path& root : roots) {
    if (!fs::exists(root)) UsageError("input does not exist: " + root.string());
    if (fs::is_regular_file(root)) {
      if (root.extension() != ".csv") UsageError("input file is not CSV: " + root.string());
      files.push_back(root);
      continue;
    }
    if (!fs::is_directory(root)) {
      UsageError("input is neither a file nor directory: " + root.string());
    }
    for (const fs::directory_entry& entry :
         fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
      if (entry.is_regular_file() && entry.path().extension() == ".csv") {
        files.push_back(entry.path());
      }
    }
  }
  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  if (files.empty()) UsageError("inputs contain no CSV files");
  return files;
}

std::uint64_t Fnv1a64(std::string_view text, std::uint64_t seed) {
  std::uint64_t hash = 14695981039346656037ULL ^ seed;
  for (const char character : text) {
    const auto byte = static_cast<unsigned char>(character);
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  // Avalanche before taking a small modulus. Raw FNV low bits retain patterns
  // from the role suffix and can assign every first-write/final-read pair to
  // the same relative stream.
  hash ^= hash >> 30;
  hash *= 0xbf58476d1ce4e5b9ULL;
  hash ^= hash >> 27;
  hash *= 0x94d049bb133111ebULL;
  hash ^= hash >> 31;
  return hash;
}

std::string CaseName(const fs::path& path) {
  const std::string stem = path.stem().string();
  const std::size_t separator = stem.find('.');
  const std::string name = stem.substr(0, separator);
  if (name.empty()) throw std::runtime_error("cannot derive case name from " + path.string());
  return name;
}

std::uint64_t CapacityFromFilename(const fs::path& path) {
  const std::string stem = path.stem().string();
  const std::size_t separator = stem.rfind('.');
  if (separator == std::string::npos || separator + 1 == stem.size()) {
    throw std::runtime_error("cannot parse capacity from filename " + path.string());
  }
  const std::string_view text(stem.data() + separator + 1, stem.size() - separator - 1);
  std::uint64_t capacity = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), capacity);
  if (error != std::errc{} || end != text.data() + text.size() || capacity == 0) {
    throw std::runtime_error("cannot parse positive capacity from filename " + path.string());
  }
  return capacity;
}

std::int64_t EventTime(std::int64_t point, bool final_access) {
  if (point > std::numeric_limits<std::int64_t>::max() / 2 ||
      point < std::numeric_limits<std::int64_t>::min() / 2 ||
      (final_access && point == std::numeric_limits<std::int64_t>::min() / 2)) {
    throw std::runtime_error("lifetime event cannot be expanded safely");
  }
  const std::int64_t doubled = point * 2;
  return final_access ? doubled - 1 : doubled;
}

dsa::ScheduledProgram BuildSyntheticProgram(const dsa::DsaProblem& problem,
                                            std::uint32_t stream_count, std::uint64_t seed,
                                            const std::string& case_name) {
  dsa::ScheduledProgram program;
  program.stream_count = stream_count;
  program.operations.reserve(problem.buffers.size() * 2);
  program.cross_dependencies.reserve(problem.buffers.size());
  program.accesses.reserve(problem.buffers.size() * 2);

  dsa::OperationId next_operation = 0;
  for (const dsa::Buffer& buffer : problem.buffers) {
    if (buffer.live_intervals.size() != 1) {
      throw std::runtime_error("synthetic MiniMalloc lift requires one interval per buffer");
    }
    const dsa::Interval& lifetime = buffer.live_intervals.front();
    const std::string identity = case_name + ":" + buffer.name + ":" + std::to_string(buffer.id);
    const dsa::StreamId definition_stream =
        static_cast<dsa::StreamId>(Fnv1a64(identity + ":first_write", seed) % stream_count);
    const dsa::StreamId final_stream =
        static_cast<dsa::StreamId>(Fnv1a64(identity + ":final_read", seed) % stream_count);
    const dsa::OperationId definition = next_operation++;
    const dsa::OperationId final_access = next_operation++;
    program.operations.push_back(
        {definition, definition_stream, 0, EventTime(lifetime.lower, false)});
    program.operations.push_back({final_access, final_stream, 0, EventTime(lifetime.upper, true)});
    program.cross_dependencies.push_back({definition, final_access});
    program.accesses.push_back({buffer.id, definition, dsa::AccessKind::kWrite});
    program.accesses.push_back({buffer.id, final_access, dsa::AccessKind::kRead});
  }

  std::vector<std::vector<std::size_t>> operations_by_stream(stream_count);
  for (std::size_t index = 0; index < program.operations.size(); ++index) {
    operations_by_stream[program.operations[index].stream].push_back(index);
  }
  for (std::vector<std::size_t>& stream : operations_by_stream) {
    std::sort(stream.begin(), stream.end(), [&](std::size_t first, std::size_t second) {
      const dsa::ScheduledOperation& first_op = program.operations[first];
      const dsa::ScheduledOperation& second_op = program.operations[second];
      return std::tie(first_op.schedule_time, first_op.id) <
             std::tie(second_op.schedule_time, second_op.id);
    });
    for (std::size_t issue_index = 0; issue_index < stream.size(); ++issue_index) {
      program.operations[stream[issue_index]].issue_index = issue_index;
    }
  }
  return program;
}

dsa::StructuredProblemDocument BuildDocument(const fs::path& path, const Options& options) {
  dsa::MiniMallocDocument input = dsa::ReadMiniMallocCsvFile(path);
  const std::string case_name = CaseName(path);
  std::string capacity_policy = "filename";
  if (options.capacity) {
    input.problem.pools.front().capacity = options.capacity;
    capacity_policy = "explicit";
  } else if (options.capacity_from_first_fit) {
    dsa::DsaProblem standard = input.problem;
    standard.pools.front().capacity.reset();
    standard.objective = dsa::MinimizePeakObjective();
    const dsa::DsaResult reference = dsa::FirstFitSolver().Solve(standard);
    if (reference.status != dsa::SolveStatus::kFeasible || !reference.solution ||
        reference.objective.max_peak == 0) {
      throw std::runtime_error("standard first fit failed to provide a capacity reference");
    }
    input.problem.pools.front().capacity = reference.objective.max_peak;
    capacity_policy = "standard_first_fit_peak";
  } else {
    input.problem.pools.front().capacity = CapacityFromFilename(path);
  }

  const dsa::ScheduledProgram program =
      BuildSyntheticProgram(input.problem, options.streams, options.seed, case_name);
  std::vector<std::vector<std::uint64_t>> sync_cost(options.streams,
                                                    std::vector<std::uint64_t>(options.streams, 1));
  for (std::uint32_t stream = 0; stream < options.streams; ++stream) {
    sync_cost[stream][stream] = 0;
  }
  const dsa::PenaltyEdgeDerivation derivation =
      dsa::DerivePenaltyEdges(input.problem, program, sync_cost);
  dsa::ApplyPenaltyEdgeDerivation(&input.problem, derivation);

  dsa::StructuredProblemDocument document;
  document.profile = dsa::BenchmarkProfile::kDsaRpV1;
  document.instance = "minimalloc_challenging_" + case_name + "_dsa_rp_k" +
                      std::to_string(options.streams) + "_seed" + std::to_string(options.seed);
  document.metadata = {
      {"corpus_family", "minimalloc-challenging"},
      {"corpus_source_path", path.generic_string()},
      {"capacity_policy", capacity_policy},
      {"dsa_rp_edge_construction", "maximal_access_happens_before_v1"},
      {"edge_semantics", "soft_pairwise_unit_sync"},
      {"generator", "dsa-rp-lift-v1"},
      {"source_commit", options.source_commit},
      {"source_format", "minimalloc_csv"},
      {"source_repo", "https://github.com/google/minimalloc"},
      {"synthetic_access_model", "one_first_write_one_final_read"},
      {"synthetic_schedule", "true"},
      {"synthetic_seed", std::to_string(options.seed)},
      {"synthetic_streams", std::to_string(options.streams)},
      {"lifetime_compatible_pairs", std::to_string(derivation.lifetime_compatible_pairs)},
      {"ordered_or_zero_cost_pairs", std::to_string(derivation.ordered_pairs)},
      {"reuse_penalties", std::to_string(derivation.edges.size())},
  };
  document.problem = std::move(input.problem);
  const std::vector<std::string> errors = dsa::ValidateStructuredProblemDocument(document);
  if (!errors.empty()) {
    throw std::runtime_error("generated invalid DSA-RP document: " + errors.front());
  }
  return document;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = ParseOptions(argc, argv);
    const std::vector<fs::path> inputs = DiscoverCsvFiles(options.inputs);
    fs::create_directories(options.output);
    for (const fs::path& input : inputs) {
      const dsa::StructuredProblemDocument document = BuildDocument(input, options);
      const fs::path output = options.output / (CaseName(input) + ".json");
      dsa::WriteStructuredProblemJsonFile(output, document);
      std::cout << output << ": buffers=" << document.problem.buffers.size()
                << " penalties=" << document.metadata.at("reuse_penalties") << '\n';
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "dsa-rp-lift: " << error.what() << '\n';
    return 1;
  }
}

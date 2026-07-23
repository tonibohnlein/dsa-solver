#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
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
#include "dsa/io/minimalloc_csv.h"
#include "dsa/model/structured_problem.h"
#include "dsa/model/validator.h"

namespace {

enum class ObjectiveOverride : std::uint8_t {
  kPeak,
  kFitCost,
};

struct Options {
  std::filesystem::path input;
  std::string solver = "first-fit";
  std::optional<std::uint64_t> capacity;
  std::optional<std::filesystem::path> output;
  std::optional<std::filesystem::path> json_output;
  std::optional<std::filesystem::path> solution_output;
  std::optional<std::filesystem::path> reference_output;
  std::optional<dsa::PoolId> core_relaxation_pool;
  std::optional<ObjectiveOverride> objective_override;
  dsa::LocalSearchOptions local_search;
  dsa::PyptoStructuredSearchOptions pypto_structured_search;
  dsa::TvmHillClimbOptions tvm_hill_climb;
  dsa::ReusePenaltyLocalSearchOptions reuse_penalty_local_search;
};

[[noreturn]] void UsageError(const std::string& message) {
  throw std::invalid_argument(message + "\nRun dsa-bench --help for usage information.");
}

std::uint64_t ParseUnsigned(std::string_view text, const std::string& option) {
  if (text.empty()) UsageError(option + " requires a value");
  if (text.front() == '-') UsageError(option + " requires a non-negative value");
  std::size_t consumed = 0;
  const std::uint64_t value = std::stoull(std::string(text), &consumed);
  if (consumed != text.size()) UsageError(option + " has an invalid integer value");
  return value;
}

void PrintHelp() {
  std::cout << "Usage: dsa-bench --input INSTANCE.{csv,json} [options]\n\n"
            << "Options:\n"
            << "  --solver first-fit|canonical-greedy|promote-repair|promote-all|"
               "unit-random-coloring|canonical-branch-and-bound|implicit-hitting-set|"
               "capacity-two-exact|span-one-min-cost-flow|treewidth-partition-dp|"
               "reuse-penalty-portfolio|reuse-penalty-local-search|cypress-relaxation|"
               "xla-heap|local-search|pypto-structured-search|tvm-hill-climb\n"
            << "                                   Solver to run (default: first-fit)\n"
            << "  --capacity BYTES                 Set the default pool capacity\n"
            << "  --output FILE.csv                Write a MiniMalloc-compatible solution\n"
            << "  --reference-output FILE.csv      Compare with a MiniMalloc solution CSV\n"
            << "  --json-output FILE.json          Also write the JSON result to a file\n"
            << "  --solution-output FILE.json      Write a replayable structured solution\n"
            << "  --core-relaxation-pool ID        Run a sound standard relaxation of one pool\n"
            << "  --seed N                         Search random seed (default: 0)\n"
            << "  --iterations N                   Search evaluations/attempts budget\n"
            << "  --restarts N                     Local-search restart count\n"
            << "  --stagnation N                   Iterations before perturbation\n"
            << "  --target-total-peak BYTES        TVM-style early-stop target\n"
            << "  --worse-move-scale PERCENT       TVM-style annealing scale (default: 50)\n"
            << "  --objective peak|fit-cost        Override the document objective\n"
            << "  --help                           Show this help\n";
}

Options ParseOptions(int argc, char** argv) {
  Options options;
  auto next = [&](int* index, const std::string& option) -> std::string_view {
    if (*index + 1 >= argc) UsageError(option + " requires a value");
    ++*index;
    return argv[*index];
  };

  for (int i = 1; i < argc; ++i) {
    const std::string option = argv[i];
    if (option == "--help") {
      PrintHelp();
      std::exit(0);
    } else if (option == "--input") {
      options.input = next(&i, option);
    } else if (option == "--solver") {
      options.solver = next(&i, option);
    } else if (option == "--capacity") {
      options.capacity = ParseUnsigned(next(&i, option), option);
    } else if (option == "--output") {
      options.output = std::filesystem::path(next(&i, option));
    } else if (option == "--json-output") {
      options.json_output = std::filesystem::path(next(&i, option));
    } else if (option == "--solution-output") {
      options.solution_output = std::filesystem::path(next(&i, option));
    } else if (option == "--reference-output") {
      options.reference_output = std::filesystem::path(next(&i, option));
    } else if (option == "--core-relaxation-pool") {
      const std::uint64_t value = ParseUnsigned(next(&i, option), option);
      if (value > std::numeric_limits<dsa::PoolId>::max()) {
        UsageError(option + " exceeds PoolId range");
      }
      options.core_relaxation_pool = static_cast<dsa::PoolId>(value);
    } else if (option == "--seed") {
      const std::uint64_t seed = ParseUnsigned(next(&i, option), option);
      options.local_search.seed = seed;
      options.pypto_structured_search.seed = seed;
      options.tvm_hill_climb.seed = seed;
      options.reuse_penalty_local_search.seed = seed;
    } else if (option == "--iterations") {
      const std::uint64_t iterations = ParseUnsigned(next(&i, option), option);
      options.local_search.max_iterations = iterations;
      options.pypto_structured_search.max_iterations = iterations;
      options.tvm_hill_climb.max_attempts = iterations;
      options.reuse_penalty_local_search.max_evaluations = iterations;
    } else if (option == "--restarts") {
      options.local_search.restarts = ParseUnsigned(next(&i, option), option);
      options.pypto_structured_search.restarts = options.local_search.restarts;
      options.reuse_penalty_local_search.restarts = options.local_search.restarts;
    } else if (option == "--stagnation") {
      options.local_search.stagnation_limit = ParseUnsigned(next(&i, option), option);
      options.pypto_structured_search.stagnation_limit = options.local_search.stagnation_limit;
      options.reuse_penalty_local_search.stagnation_limit = options.local_search.stagnation_limit;
    } else if (option == "--target-total-peak") {
      options.tvm_hill_climb.target_total_peak = ParseUnsigned(next(&i, option), option);
    } else if (option == "--worse-move-scale") {
      const std::uint64_t scale = ParseUnsigned(next(&i, option), option);
      if (scale > 100) UsageError(option + " must be between 0 and 100");
      options.tvm_hill_climb.worse_move_scale_percent = static_cast<std::uint32_t>(scale);
    } else if (option == "--objective") {
      const std::string value(next(&i, option));
      if (value == "peak") {
        options.objective_override = ObjectiveOverride::kPeak;
      } else if (value == "fit-cost") {
        options.objective_override = ObjectiveOverride::kFitCost;
      } else {
        UsageError("unknown objective '" + value + "'");
      }
    } else {
      UsageError("unknown option '" + option + "'");
    }
  }
  if (options.input.empty()) UsageError("--input is required");
  if (options.solver != "first-fit" && options.solver != "canonical-greedy" &&
      options.solver != "promote-repair" && options.solver != "promote-all" &&
      options.solver != "unit-random-coloring" && options.solver != "canonical-branch-and-bound" &&
      options.solver != "implicit-hitting-set" && options.solver != "capacity-two-exact" &&
      options.solver != "span-one-min-cost-flow" && options.solver != "treewidth-partition-dp" &&
      options.solver != "reuse-penalty-portfolio" &&
      options.solver != "reuse-penalty-local-search" && options.solver != "cypress-relaxation" &&
      options.solver != "xla-heap" && options.solver != "local-search" &&
      options.solver != "pypto-structured-search" && options.solver != "tvm-hill-climb") {
    UsageError("unknown solver '" + options.solver + "'");
  }
  return options;
}

std::string JsonEscape(std::string_view value) {
  std::ostringstream output;
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    switch (character) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (character < 0x20) {
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<unsigned int>(character) << std::dec;
        } else {
          output << static_cast<char>(character);
        }
    }
  }
  return output.str();
}

dsa::StructuredProblemDocument LoadProblemDocument(const Options& options) {
  dsa::StructuredProblemDocument document;
  if (options.input.extension() == ".json") {
    document = dsa::ReadStructuredProblemJsonFile(options.input);
  } else {
    dsa::MiniMallocDocument csv = dsa::ReadMiniMallocCsvFile(options.input);
    document.profile = dsa::BenchmarkProfile::kStandardDsa;
    document.instance = options.input.filename().string();
    document.metadata["source_format"] = "minimalloc_csv";
    document.problem = std::move(csv.problem);
  }

  if (options.core_relaxation_pool) {
    const std::vector<dsa::StructuredProblemDocument> relaxations =
        dsa::BuildCoreRelaxations(document);
    const std::string requested = std::to_string(*options.core_relaxation_pool);
    const auto found = std::find_if(
        relaxations.begin(), relaxations.end(), [&](const dsa::StructuredProblemDocument& value) {
          const auto pool = value.metadata.find("source_pool_id");
          return pool != value.metadata.end() && pool->second == requested;
        });
    if (found == relaxations.end()) {
      throw std::runtime_error("structured problem has no fixed pool " + requested + " to relax");
    }
    document = *found;
  }

  if (options.capacity) {
    if (document.problem.pools.size() != 1) {
      throw std::runtime_error("--capacity requires a single-pool input or core relaxation");
    }
    document.problem.pools.front().capacity = options.capacity;
  }
  if (options.objective_override == ObjectiveOverride::kPeak) {
    document.problem.objective = dsa::MinimizePeakObjective();
  } else if (options.objective_override == ObjectiveOverride::kFitCost) {
    document.problem.objective = dsa::FitThenMinimizeReuseCostObjective();
  }
  const std::vector<std::string> errors = dsa::ValidateStructuredProblemDocument(document);
  if (!errors.empty()) throw std::runtime_error("invalid benchmark profile: " + errors.front());
  return document;
}

std::optional<std::uint64_t> ReferencePeak(const Options& options,
                                           const dsa::DsaProblem& instance) {
  if (!options.reference_output) return std::nullopt;
  dsa::MiniMallocDocument reference = dsa::ReadMiniMallocCsvFile(*options.reference_output);
  if (reference.problem.buffers.size() != instance.buffers.size()) {
    throw std::runtime_error("reference CSV describes a different number of buffers");
  }
  for (std::size_t i = 0; i < instance.buffers.size(); ++i) {
    const dsa::Buffer& expected = instance.buffers[i];
    const dsa::Buffer& actual = reference.problem.buffers[i];
    if (expected.name != actual.name || expected.size != actual.size ||
        expected.live_intervals.size() != 1 || actual.live_intervals.size() != 1 ||
        expected.live_intervals.front().lower != actual.live_intervals.front().lower ||
        expected.live_intervals.front().upper != actual.live_intervals.front().upper) {
      throw std::runtime_error("reference CSV does not describe the input instance at row " +
                               std::to_string(i + 2));
    }
  }
  if (!reference.solution) {
    throw std::runtime_error("reference CSV has no offset column");
  }
  const dsa::DsaSolution reference_solution =
      std::move(reference.solution).value_or(dsa::DsaSolution{});
  const std::vector<std::string> errors =
      dsa::ValidateSolution(reference.problem, reference_solution);
  if (!errors.empty()) {
    throw std::runtime_error("reference solution is invalid: " + errors.front());
  }
  return dsa::EvaluateObjective(reference.problem, reference_solution).max_peak;
}

void AppendStringArray(std::ostringstream* output, const std::vector<std::string>& values) {
  *output << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) *output << ',';
    *output << '"' << JsonEscape(values[i]) << '"';
  }
  *output << ']';
}

void AppendUnsignedMap(std::ostringstream* output,
                       const std::map<std::string, std::uint64_t>& values) {
  *output << '{';
  std::size_t index = 0;
  for (const auto& [name, value] : values) {
    if (index++ != 0) *output << ',';
    *output << '"' << JsonEscape(name) << "\":" << value;
  }
  *output << '}';
}

std::string BuildJson(const Options& options, const dsa::StructuredProblemDocument& document,
                      const dsa::SolverCompatibility& compatibility, const dsa::DsaResult& result,
                      std::uint64_t runtime_microseconds,
                      std::optional<std::uint64_t> reference_peak) {
  const dsa::DsaProblem& problem = document.problem;
  std::ostringstream output;
  output << '{' << "\"instance\":\"" << JsonEscape(document.instance) << "\","
         << "\"schema_version\":" << document.schema_version << ',' << "\"profile\":\""
         << dsa::ToString(document.profile) << "\","
         << "\"solver\":\"" << JsonEscape(options.solver) << "\","
         << "\"status\":\"" << dsa::ToString(result.status) << "\","
         << "\"buffers\":" << problem.buffers.size() << ','
         << "\"peak\":" << result.objective.max_peak << ','
         << "\"total_peak\":" << result.objective.total_peak << ',' << "\"capacity_overflow\":"
         << dsa::EvaluateObjectiveMetric(problem, result.objective,
                                         dsa::ObjectiveMetric::kCapacityOverflow)
         << ',' << "\"reuse_cost\":" << result.objective.reuse_cost << ','
         << "\"bank_cost\":" << result.objective.bank_cost << ','
         << "\"runtime_us\":" << runtime_microseconds << ','
         << "\"seed\":" << options.local_search.seed << ',' << "\"structurally_compatible\":"
         << (compatibility.StructurallyCompatible() ? "true" : "false") << ','
         << "\"objective_compatible\":" << (compatibility.ObjectiveCompatible() ? "true" : "false")
         << ',' << "\"required_features\":";
  AppendStringArray(&output, compatibility.required_features);
  output << ",\"unsupported_features\":";
  AppendStringArray(&output, compatibility.unsupported_features);
  output << ",\"unsupported_objectives\":";
  AppendStringArray(&output, compatibility.unsupported_objectives);
  output << ",\"diagnostics\":";
  AppendStringArray(&output, result.diagnostics);
  output << ",\"solver_metrics\":";
  AppendUnsignedMap(&output, result.solver_metrics);
  output << ",\"objective_terms\":[";
  for (std::size_t i = 0; i < problem.objective.terms.size(); ++i) {
    if (i != 0) output << ',';
    output << '"' << dsa::ToString(problem.objective.terms[i]) << '"';
  }
  output << ']';
  if (document.relaxed_from) {
    output << ",\"relaxed_from\":\"" << JsonEscape(*document.relaxed_from) << '"';
  }
  if (!document.relaxed_features.empty()) {
    output << ",\"relaxed_features\":";
    AppendStringArray(&output, document.relaxed_features);
  }
  if (options.solver == "local-search" || options.solver == "pypto-structured-search") {
    output << ",\"search_budget\":" << options.local_search.max_iterations
           << ",\"restarts\":" << options.local_search.restarts
           << ",\"stagnation_limit\":" << options.local_search.stagnation_limit;
  } else if (options.solver == "reuse-penalty-local-search") {
    output << ",\"search_budget\":" << options.reuse_penalty_local_search.max_evaluations
           << ",\"restarts\":" << options.reuse_penalty_local_search.restarts
           << ",\"stagnation_limit\":" << options.reuse_penalty_local_search.stagnation_limit;
  } else if (options.solver == "tvm-hill-climb") {
    output << ",\"search_budget\":" << options.tvm_hill_climb.max_attempts
           << ",\"worse_move_scale_percent\":" << options.tvm_hill_climb.worse_move_scale_percent;
    if (options.tvm_hill_climb.target_total_peak) {
      output << ",\"target_total_peak\":" << *options.tvm_hill_climb.target_total_peak;
    }
  }
  if (reference_peak) {
    const long double gap = static_cast<long double>(result.objective.max_peak) -
                            static_cast<long double>(*reference_peak);
    const long double gap_percent = *reference_peak == 0 ? 0 : 100.0L * gap / *reference_peak;
    output << ",\"reference_peak\":" << *reference_peak << ",\"gap_bytes\":" << std::fixed
           << std::setprecision(0) << gap << ",\"gap_percent\":" << std::setprecision(6)
           << gap_percent;
  }
  output << '}';
  return output.str();
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = ParseOptions(argc, argv);
    dsa::StructuredProblemDocument document = LoadProblemDocument(options);

    std::unique_ptr<dsa::DsaSolver> solver;
    if (options.solver == "canonical-greedy") {
      solver = std::make_unique<dsa::CanonicalGreedySolver>();
    } else if (options.solver == "promote-repair") {
      solver = std::make_unique<dsa::PromoteRepairSolver>();
    } else if (options.solver == "promote-all") {
      solver = std::make_unique<dsa::PromoteAllSolver>();
    } else if (options.solver == "unit-random-coloring") {
      dsa::UnitRandomColoringOptions random_options;
      random_options.seed = options.local_search.seed;
      solver = std::make_unique<dsa::UnitRandomColoringSolver>(random_options);
    } else if (options.solver == "canonical-branch-and-bound") {
      dsa::CanonicalBranchAndBoundOptions exact_options;
      exact_options.max_search_nodes = options.local_search.max_iterations;
      solver = std::make_unique<dsa::CanonicalBranchAndBoundSolver>(exact_options);
    } else if (options.solver == "implicit-hitting-set") {
      dsa::ImplicitHittingSetOptions exact_options;
      exact_options.max_oracle_nodes = options.local_search.max_iterations;
      solver = std::make_unique<dsa::ImplicitHittingSetSolver>(exact_options);
    } else if (options.solver == "capacity-two-exact") {
      dsa::CapacityTwoExactOptions exact_options;
      exact_options.max_search_nodes = options.local_search.max_iterations;
      solver = std::make_unique<dsa::CapacityTwoExactSolver>(exact_options);
    } else if (options.solver == "span-one-min-cost-flow") {
      solver = std::make_unique<dsa::SpanOneMinCostFlowSolver>();
    } else if (options.solver == "treewidth-partition-dp") {
      solver = std::make_unique<dsa::TreewidthPartitionDpSolver>();
    } else if (options.solver == "reuse-penalty-portfolio") {
      dsa::ReusePenaltyPortfolioOptions portfolio_options;
      portfolio_options.capacity_two.max_search_nodes = options.local_search.max_iterations;
      portfolio_options.general.max_search_nodes = options.local_search.max_iterations;
      solver = std::make_unique<dsa::ReusePenaltyPortfolioSolver>(portfolio_options);
    } else if (options.solver == "reuse-penalty-local-search") {
      solver =
          std::make_unique<dsa::ReusePenaltyLocalSearchSolver>(options.reuse_penalty_local_search);
    } else if (options.solver == "cypress-relaxation") {
      solver = std::make_unique<dsa::CypressRelaxationSolver>();
    } else if (options.solver == "xla-heap") {
      solver = std::make_unique<dsa::XlaHeapSolver>();
    } else if (options.solver == "pypto-structured-search") {
      solver = std::make_unique<dsa::PyptoStructuredSearchSolver>(options.pypto_structured_search);
    } else if (options.solver == "local-search") {
      solver = std::make_unique<dsa::LocalSearchSolver>(options.local_search);
    } else if (options.solver == "tvm-hill-climb") {
      solver = std::make_unique<dsa::TvmHillClimbSolver>(options.tvm_hill_climb);
    } else {
      solver = std::make_unique<dsa::FirstFitSolver>();
    }
    const dsa::SolverCompatibility compatibility =
        dsa::CheckSolverCompatibility(document.problem, solver->Capabilities());

    const auto started = std::chrono::steady_clock::now();
    const dsa::DsaResult result = solver->Solve(document.problem);
    const auto stopped = std::chrono::steady_clock::now();
    const auto runtime = std::chrono::duration_cast<std::chrono::microseconds>(stopped - started);

    if (result.solution) {
      const dsa::DsaProblem validation_problem = [&] {
        dsa::DsaProblem copy = document.problem;
        if (result.status == dsa::SolveStatus::kBestEffortNoFit) {
          for (dsa::Pool& pool : copy.pools) pool.capacity.reset();
        }
        return copy;
      }();
      const std::vector<std::string> validation =
          dsa::ValidateSolution(validation_problem, *result.solution);
      if (!validation.empty()) {
        throw std::runtime_error("solver returned an invalid solution: " + validation.front());
      }
      if (options.output) {
        if (dsa::IsPyptoProfile(document.profile)) {
          throw std::runtime_error(
              "--output CSV is unavailable for a structured profile; use a core relaxation");
        }
        dsa::WriteMiniMallocCsvFile(*options.output, document.problem, &*result.solution);
      }
      if (options.solution_output) {
        dsa::WriteStructuredSolutionJsonFile(
            *options.solution_output,
            dsa::BuildStructuredSolutionDocument(document, *result.solution,
                                                 {{"solver", options.solver}}));
      }
    }

    if (options.reference_output && dsa::IsPyptoProfile(document.profile)) {
      throw std::runtime_error(
          "MiniMalloc references are only comparable with standard or core-relaxation profiles");
    }
    const std::string json = BuildJson(options, document, compatibility, result,
                                       static_cast<std::uint64_t>(runtime.count()),
                                       ReferencePeak(options, document.problem));
    std::cout << json << '\n';
    if (options.json_output) {
      std::ofstream output(*options.json_output);
      if (!output)
        throw std::runtime_error("cannot open JSON output: " + options.json_output->string());
      output << json << '\n';
    }
    return result.status == dsa::SolveStatus::kFeasible ? 0 : 2;
  } catch (const std::exception& error) {
    std::cerr << "dsa-bench: " << error.what() << '\n';
    return 1;
  }
}

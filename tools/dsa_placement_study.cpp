#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "dsa/algorithms/first_fit_solver.h"
#include "dsa/analysis/reuse_geometry.h"
#include "dsa/model/structured_problem.h"
#include "dsa/model/validator.h"

namespace {

struct Options {
  std::filesystem::path input;
  std::optional<std::filesystem::path> compact_output;
  std::optional<std::filesystem::path> sparse_output;
  std::size_t max_passes = 8;
};

[[noreturn]] void UsageError(const std::string& message) {
  throw std::invalid_argument(message + "\nRun dsa-placement-study --help for usage information.");
}

std::size_t ParseSize(std::string_view text, const std::string& option) {
  if (text.empty() || text.front() == '-') UsageError(option + " requires a non-negative value");
  std::size_t consumed = 0;
  const unsigned long long value = std::stoull(std::string(text), &consumed);
  if (consumed != text.size() || value > std::numeric_limits<std::size_t>::max()) {
    UsageError(option + " has an invalid value");
  }
  return static_cast<std::size_t>(value);
}

void PrintHelp() {
  std::cout << "Usage: dsa-placement-study --input INSTANCE.json [options]\n\n"
            << "Construct compact and low-reuse reference placements for a controlled "
               "PTOAS study.\n\n"
            << "Options:\n"
            << "  --compact-output FILE.json  Write the compact replay solution\n"
            << "  --sparse-output FILE.json   Write the sparse replay solution\n"
            << "  --max-passes N              Sparse coordinate passes (default: 8)\n"
            << "  --help                      Show this help\n";
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
    }
    if (option == "--input") {
      options.input = next(&index, option);
    } else if (option == "--compact-output") {
      options.compact_output = std::filesystem::path(next(&index, option));
    } else if (option == "--sparse-output") {
      options.sparse_output = std::filesystem::path(next(&index, option));
    } else if (option == "--max-passes") {
      options.max_passes = ParseSize(next(&index, option), option);
    } else {
      UsageError("unknown option '" + option + "'");
    }
  }
  if (options.input.empty()) UsageError("--input is required");
  return options;
}

std::string JsonEscape(std::string_view value) {
  std::ostringstream output;
  for (char character : value) {
    if (character == '"' || character == '\\') output << '\\';
    output << character;
  }
  return output.str();
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = ParseOptions(argc, argv);
    const dsa::StructuredProblemDocument document =
        dsa::ReadStructuredProblemJsonFile(options.input);
    const dsa::DsaResult compact = dsa::FirstFitSolver().Solve(document.problem);
    if (compact.status != dsa::SolveStatus::kFeasible || !compact.solution) {
      std::cout << "{\"instance\":\"" << JsonEscape(document.instance)
                << "\",\"status\":\"compact_" << dsa::ToString(compact.status) << "\"}\n";
      return 2;
    }
    const std::vector<std::string> compact_errors =
        dsa::ValidateSolution(document.problem, *compact.solution);
    if (!compact_errors.empty()) {
      throw std::runtime_error("compact placement is invalid: " + compact_errors.front());
    }

    const dsa::SparseReferenceResult sparse = dsa::BuildSparseReferencePlacement(
        document.problem, *compact.solution, options.max_passes);
    const dsa::ObjectiveValue sparse_objective =
        dsa::EvaluateObjective(document.problem, sparse.solution);
    if (options.compact_output) {
      dsa::WriteStructuredSolutionJsonFile(
          *options.compact_output,
          dsa::BuildStructuredSolutionDocument(
              document, *compact.solution,
              {{"reference", "compact_first_fit"}, {"study", "physical_reuse_geometry_v1"}}));
    }
    if (options.sparse_output) {
      dsa::WriteStructuredSolutionJsonFile(
          *options.sparse_output,
          dsa::BuildStructuredSolutionDocument(
              document, sparse.solution,
              {{"reference", "sparse_coordinate_descent"},
               {"study", "physical_reuse_geometry_v1"}}));
    }

    std::cout << "{\"instance\":\"" << JsonEscape(document.instance)
              << "\",\"status\":\"ok\",\"buffers\":" << document.problem.buffers.size()
              << ",\"compact_peak\":" << compact.objective.max_peak
              << ",\"sparse_peak\":" << sparse_objective.max_peak
              << ",\"compact_reuse_pairs\":" << sparse.initial.pair_count
              << ",\"sparse_reuse_pairs\":" << sparse.final.pair_count
              << ",\"compact_overlap_bytes\":" << sparse.initial.overlap_bytes
              << ",\"sparse_overlap_bytes\":" << sparse.final.overlap_bytes
              << ",\"accepted_moves\":" << sparse.accepted_moves
              << ",\"passes\":" << sparse.passes << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "dsa-placement-study: " << error.what() << '\n';
    return 1;
  }
}

// Copyright 2026 DSA-Solver Contributors
// SPDX-License-Identifier: Apache-2.0

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "dsa/model/architecture.h"
#include "dsa/model/structured_problem.h"

namespace {

struct Options {
  std::filesystem::path program;
  std::filesystem::path architecture;
  std::filesystem::path output;
};

[[noreturn]] void UsageError(const std::string& message) {
  throw std::invalid_argument(message + "\nRun dsa-bind --help for usage information.");
}

void PrintHelp() {
  std::cout << "Usage: dsa-bind --program PROGRAM.json --architecture ARCH.json "
               "--output BOUND.json\n\n"
            << "Bind an architecture-free PyPTO DSA program to a versioned architecture "
               "specification.\n";
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
    } else if (option == "--program") {
      options.program = next(&i, option);
    } else if (option == "--architecture") {
      options.architecture = next(&i, option);
    } else if (option == "--output") {
      options.output = next(&i, option);
    } else {
      UsageError("unknown option '" + option + "'");
    }
  }
  if (options.program.empty()) UsageError("--program is required");
  if (options.architecture.empty()) UsageError("--architecture is required");
  if (options.output.empty()) UsageError("--output is required");
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = ParseOptions(argc, argv);
    const dsa::StructuredProblemDocument program =
        dsa::ReadStructuredProblemJsonFile(options.program);
    const dsa::ArchitectureSpec architecture =
        dsa::ReadArchitectureSpecJsonFile(options.architecture);
    const dsa::StructuredProblemDocument bound = dsa::BindArchitecture(program, architecture);
    dsa::WriteStructuredProblemJsonFile(options.output, bound);
    std::cout << "dsa-bind: wrote " << options.output
              << " (program=" << bound.metadata.at("program_fingerprint_fnv1a64")
              << ", architecture=" << bound.metadata.at("architecture_fingerprint_fnv1a64")
              << ")\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "dsa-bind: " << error.what() << '\n';
    return 1;
  }
}

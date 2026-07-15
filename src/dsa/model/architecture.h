// Copyright 2026 DSA-Solver Contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef DSA_MODEL_ARCHITECTURE_H_
#define DSA_MODEL_ARCHITECTURE_H_

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "dsa/model/model.h"
#include "dsa/model/structured_problem.h"

namespace dsa {

inline constexpr std::uint32_t kArchitectureSpecSchemaVersion = 1;

struct ArchitectureMemorySpace {
  // Matches the compiler-facing Pool::name (for example Vec, Mat, Left,
  // Right, or Acc). Hardware display names remain metadata.
  std::string logical_space;
  std::uint64_t usable_capacity = 0;
  std::optional<std::uint64_t> physical_capacity;
  std::uint64_t minimum_alignment = 1;
  std::vector<AddressRange> reserved_ranges;
  std::optional<BankGeometry> bank_geometry;
};

struct ArchitectureSpec {
  std::uint32_t schema_version = kArchitectureSpecSchemaVersion;
  std::string architecture_id;
  std::vector<std::string> supported_lowering_abis;
  std::map<std::string, std::string> metadata;
  std::vector<ArchitectureMemorySpace> memory_spaces;

  [[nodiscard]] const ArchitectureMemorySpace* FindMemorySpace(
      const std::string& logical_space) const noexcept;
};

[[nodiscard]] std::vector<std::string> ValidateArchitectureSpec(
    const ArchitectureSpec& architecture);
[[nodiscard]] ArchitectureSpec ReadArchitectureSpecJson(std::istream& input);
[[nodiscard]] ArchitectureSpec ReadArchitectureSpecJsonFile(const std::filesystem::path& path);
void WriteArchitectureSpecJson(std::ostream& output, const ArchitectureSpec& architecture);
void WriteArchitectureSpecJsonFile(const std::filesystem::path& path,
                                   const ArchitectureSpec& architecture);

// An unbound program uses the ordinary structured-problem schema, but every
// pool capacity is null, architecture-derived bank geometry is absent,
// metadata.target is absent, and metadata.lowering_abi is present.
[[nodiscard]] std::vector<std::string> ValidateUnboundProgram(
    const StructuredProblemDocument& program);

// Fingerprints exclude source-only metadata and, for a program, all
// architecture resources. They are stable FNV-1a-64 lowercase hex strings.
[[nodiscard]] std::string FingerprintUnboundProgram(const StructuredProblemDocument& program);
[[nodiscard]] std::string FingerprintArchitectureSpec(const ArchitectureSpec& architecture);

// Bind a validated architecture-free PyPTO program to usable capacities,
// minimum alignments, reservations, and optional bank geometry. The output is
// an ordinary solver-facing StructuredProblemDocument.
[[nodiscard]] StructuredProblemDocument BindArchitecture(const StructuredProblemDocument& program,
                                                         const ArchitectureSpec& architecture);

}  // namespace dsa

#endif  // DSA_MODEL_ARCHITECTURE_H_

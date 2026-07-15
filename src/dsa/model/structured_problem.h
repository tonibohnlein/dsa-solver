#ifndef DSA_STRUCTURED_PROBLEM_H_
#define DSA_STRUCTURED_PROBLEM_H_

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "dsa/model/model.h"

namespace dsa {

inline constexpr std::uint32_t kStructuredProblemSchemaVersion = 1;

enum class BenchmarkProfile : std::uint8_t {
  // MiniMalloc-compatible fixed-size, single-pool DSA. Results may be compared
  // directly with exact standard solvers.
  kStandardDsa,

  // Legacy schema-v1 compiler profile. Kept readable for existing artifacts;
  // new producers must choose the explicit hard or research profile below.
  kPyptoStructured,

  // Versioned production contract: only constraints currently required for a
  // device-correct PyPTO placement. Speculative cost/placement extensions are
  // rejected by profile validation.
  kPyptoHardV1,

  // PyPTO hard-v1 plus explicitly experimental constraints or cost overlays.
  // Results are research evidence and must not silently define production
  // compiler behavior.
  kPyptoResearchV1,

  // A documented relaxation of one pool from a structured instance. Its result
  // is a lower bound, never a valid structured PyPTO placement claim.
  kPyptoCoreRelaxation,
};

struct StructuredProblemDocument {
  std::uint32_t schema_version = kStructuredProblemSchemaVersion;
  BenchmarkProfile profile = BenchmarkProfile::kPyptoHardV1;
  std::string instance;
  std::optional<std::string> relaxed_from;
  std::vector<std::string> relaxed_features;
  std::map<std::string, std::string> metadata;
  DsaProblem problem;
};

[[nodiscard]] const char* ToString(BenchmarkProfile profile) noexcept;
[[nodiscard]] bool IsPyptoProfile(BenchmarkProfile profile) noexcept;

// Validates the envelope and profile contract in addition to ValidateProblem.
// In particular, standard and core-relaxation documents must remain directly
// representable by the MiniMalloc problem format.
[[nodiscard]] std::vector<std::string> ValidateStructuredProblemDocument(
    const StructuredProblemDocument& document);

[[nodiscard]] StructuredProblemDocument ReadStructuredProblemJson(std::istream& input);
[[nodiscard]] StructuredProblemDocument ReadStructuredProblemJsonFile(
    const std::filesystem::path& path);
void WriteStructuredProblemJson(std::ostream& output, const StructuredProblemDocument& document);
void WriteStructuredProblemJsonFile(const std::filesystem::path& path,
                                    const StructuredProblemDocument& document);

// Produce one sound standard-DSA lower-bound document per fixed pool. Hard
// compiler constraints, alignment, reserved ranges, cost overlays, and
// cross-interval identity are relaxed explicitly and listed in
// relaxed_features. Temporal exclusions, colocations, overlapping intervals of
// one buffer, and flexible pool assignment cannot be projected soundly by
// schema v1 and are rejected.
[[nodiscard]] std::vector<StructuredProblemDocument> BuildCoreRelaxations(
    const StructuredProblemDocument& source);

}  // namespace dsa

#endif  // DSA_STRUCTURED_PROBLEM_H_

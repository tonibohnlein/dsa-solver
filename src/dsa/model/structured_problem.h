#ifndef DSA_STRUCTURED_PROBLEM_H_
#define DSA_STRUCTURED_PROBLEM_H_

#include <cstddef>
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

struct StructuredSolutionDocument {
  std::uint32_t schema_version = kStructuredProblemSchemaVersion;
  BenchmarkProfile profile = BenchmarkProfile::kPyptoHardV1;
  std::string instance;
  std::string problem_fingerprint;
  std::map<std::string, std::string> metadata;
  DsaSolution solution;
};

struct PipelineIntentRelaxation {
  StructuredProblemDocument document;
  std::size_t removed_pipeline_reason_count = 0;
  std::size_t relaxed_separation_count = 0;
  std::size_t added_penalty_count = 0;
};

struct CrossPipeReuseVariants {
  StructuredProblemDocument hard;
  StructuredProblemDocument soft;
  std::size_t edge_count = 0;
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

// Stable fingerprint of the complete structured problem document. Replay
// artifacts use it to reject placements produced for different buffers,
// lifetimes, capacities, constraints, objectives, or target metadata.
[[nodiscard]] std::string FingerprintStructuredProblem(const StructuredProblemDocument& document);

[[nodiscard]] StructuredSolutionDocument BuildStructuredSolutionDocument(
    const StructuredProblemDocument& problem, const DsaSolution& solution,
    std::map<std::string, std::string> metadata = {});
[[nodiscard]] StructuredSolutionDocument ReadStructuredSolutionJson(std::istream& input);
[[nodiscard]] StructuredSolutionDocument ReadStructuredSolutionJsonFile(
    const std::filesystem::path& path);
void WriteStructuredSolutionJson(std::ostream& output, const StructuredSolutionDocument& document);
void WriteStructuredSolutionJsonFile(const std::filesystem::path& path,
                                     const StructuredSolutionDocument& document);

// Validate a replay artifact against the freshly exported problem and return
// its independently validated placement.
[[nodiscard]] DsaSolution ValidateAndExtractStructuredSolution(
    const StructuredProblemDocument& problem, const StructuredSolutionDocument& solution_document);

// Produce one sound standard-DSA lower-bound document per fixed pool. Hard
// compiler constraints, alignment, reserved ranges, cost overlays, and
// cross-interval identity are relaxed explicitly and listed in
// relaxed_features. Temporal exclusions, colocations, overlapping intervals of
// one buffer, and flexible pool assignment cannot be projected soundly by
// schema v1 and are rejected.
[[nodiscard]] std::vector<StructuredProblemDocument> BuildCoreRelaxations(
    const StructuredProblemDocument& source);

// Convert pipeline-stage keep-apart constraints into soft reuse costs after a
// strict capacity-constrained solve fails. All non-pipeline reasons remain
// hard. This is an explicit research relaxation, never an implicit weakening
// performed by an ordinary DSA solver.
[[nodiscard]] PipelineIntentRelaxation BuildPipelineIntentRelaxation(
    const StructuredProblemDocument& source, std::uint64_t penalty_per_pair = 1);

// Construct an A/B benchmark pair from mechanically recognized cross-resource
// WAR/WAW edges. The soft variant retains cross_pipe penalties. The hard
// variant replaces exactly those records with permanent cross_pipe
// separations while preserving unrelated constraints and costs.
[[nodiscard]] CrossPipeReuseVariants BuildCrossPipeReuseVariants(
    const StructuredProblemDocument& source);

}  // namespace dsa

#endif  // DSA_STRUCTURED_PROBLEM_H_

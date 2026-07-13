#ifndef DSA_MODEL_H_
#define DSA_MODEL_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dsa {

using BufferId = std::uint32_t;
using PoolId = std::uint32_t;

constexpr PoolId kDefaultPool = 0;

// Lifetimes are half-open [lower, upper), matching MiniMalloc. A compiler with
// inclusive [definition, last-use] points (including PyPTO) converts the upper
// endpoint to last_use + 1 at its adapter boundary.
struct Interval {
  std::int64_t lower = 0;
  std::int64_t upper = 0;

  [[nodiscard]] bool Overlaps(const Interval& other) const noexcept {
    return lower < other.upper && other.lower < upper;
  }
};

// Half-open byte range [begin, end).
struct AddressRange {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
};

struct BankGeometry {
  std::uint64_t bank_size = 0;
  std::uint32_t num_banks = 0;
};

struct Buffer {
  BufferId id = 0;
  std::string name;
  std::uint64_t size = 0;
  std::uint64_t alignment = 1;
  std::vector<Interval> live_intervals;

  // One entry means a fixed pool. Multiple entries represent future
  // memory-space selection; baseline solvers advertise that they do not yet
  // support that capability.
  std::vector<PoolId> allowed_pools{kDefaultPool};
};

struct Pool {
  PoolId id = kDefaultPool;
  std::string name = "default";
  std::optional<std::uint64_t> capacity;
  std::vector<AddressRange> reserved_ranges;
  std::optional<BankGeometry> bank_geometry;
};

struct Colocation {
  BufferId first = 0;
  BufferId second = 0;
};

struct Separation {
  BufferId first = 0;
  BufferId second = 0;
};

// A compiler proof that two buffers cannot be live on the same control-flow
// path, even if their conservative interval hulls overlap. This carries
// PyPTO's branch/phi exclusivity without weakening unrelated conflicts.
struct TemporalExclusion {
  BufferId first = 0;
  BufferId second = 0;
};

struct PinnedAllocation {
  BufferId buffer = 0;
  PoolId pool = kDefaultPool;
  std::uint64_t offset = 0;

  // When true, no other buffer may use the pinned address range, even outside
  // this buffer's lifetime. This models whole-program reserved allocations.
  bool exclusive_for_all_time = false;
};

enum class ReusePenaltyReason : std::uint8_t {
  kGeneric,
  kCrossPipe,
  kCrossCore,
  kEventBudget,
};

struct ReusePenalty {
  BufferId first = 0;
  BufferId second = 0;
  std::uint64_t cost = 0;
  ReusePenaltyReason reason = ReusePenaltyReason::kGeneric;
};

struct CostModel {
  std::vector<ReusePenalty> reuse_penalties;
};

// Objective components are recorded separately rather than prematurely
// scalarized. A solver evaluates them in the listed order.
enum class ObjectiveMetric : std::uint8_t {
  kCapacityOverflow,
  kTotalPeak,
  kMaxPeak,
  kReuseCost,
  kBankCost,
};

enum class ObjectiveAggregation : std::uint8_t {
  // Compare the first differing metric. This is the only aggregation supported
  // by schema v1; future schema versions can add weighted/Pareto modes without
  // changing the meaning of existing documents.
  kLexicographic,
};

struct ObjectiveSpec {
  ObjectiveAggregation aggregation = ObjectiveAggregation::kLexicographic;
  std::vector<ObjectiveMetric> terms{ObjectiveMetric::kTotalPeak, ObjectiveMetric::kMaxPeak};
};

// Portable DSA problem plus optional compiler-specific structure. MiniMalloc
// CSV instances populate only buffers and one pool; PyPTO can additionally
// populate multiple intervals, hard alias constraints, pipeline separations,
// pinned allocations, reserved ranges, and reuse penalties.
struct DsaProblem {
  std::vector<Buffer> buffers;
  std::vector<Pool> pools{{}};
  std::vector<Colocation> colocations;
  std::vector<Separation> separations;
  std::vector<TemporalExclusion> temporal_exclusions;
  std::vector<PinnedAllocation> pinned_allocations;
  std::optional<CostModel> cost_model;
  ObjectiveSpec objective;

  [[nodiscard]] const Buffer* FindBuffer(BufferId id) const noexcept;
  [[nodiscard]] const Pool* FindPool(PoolId id) const noexcept;
};

struct Placement {
  PoolId pool = kDefaultPool;
  std::uint64_t offset = 0;

  [[nodiscard]] bool operator==(const Placement& other) const noexcept {
    return pool == other.pool && offset == other.offset;
  }
  [[nodiscard]] bool operator!=(const Placement& other) const noexcept { return !(*this == other); }
};

struct DsaSolution {
  std::map<BufferId, Placement> placements;

  [[nodiscard]] const Placement* Find(BufferId id) const noexcept;
};

struct ObjectiveValue {
  std::map<PoolId, std::uint64_t> peak_by_pool;
  std::uint64_t total_peak = 0;
  std::uint64_t max_peak = 0;
  std::uint64_t reuse_cost = 0;
  std::uint64_t bank_cost = 0;
};

enum class SolveStatus : std::uint8_t {
  kFeasible,
  kInfeasibleProven,
  kBestEffortNoFit,
  kTimeout,
  kUnsupported,
  kInvalidProblem,
};

struct DsaResult {
  SolveStatus status = SolveStatus::kUnsupported;
  std::optional<DsaSolution> solution;
  ObjectiveValue objective;
  std::vector<std::string> diagnostics;
};

[[nodiscard]] const char* ToString(SolveStatus status) noexcept;
[[nodiscard]] const char* ToString(ObjectiveMetric metric) noexcept;
[[nodiscard]] const char* ToString(ObjectiveAggregation aggregation) noexcept;

[[nodiscard]] ObjectiveSpec MinimizePeakObjective();
[[nodiscard]] ObjectiveSpec FitThenMinimizeReuseCostObjective();

}  // namespace dsa

#endif  // DSA_MODEL_H_

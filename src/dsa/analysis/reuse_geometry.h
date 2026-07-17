#ifndef DSA_REUSE_GEOMETRY_H_
#define DSA_REUSE_GEOMETRY_H_

#include <cstddef>
#include <cstdint>

#include "dsa/model/model.h"

namespace dsa {

// Placement-only statistics for physical address reuse between allocation
// classes that are allowed to reuse memory in time.
struct ReuseGeometryStats {
  std::uint64_t pair_count = 0;
  std::uint64_t overlap_bytes = 0;
};

struct SparseReferenceResult {
  DsaSolution solution;
  ReuseGeometryStats initial;
  ReuseGeometryStats final;
  std::size_t accepted_moves = 0;
  std::size_t passes = 0;
};

[[nodiscard]] ReuseGeometryStats EvaluateReuseGeometry(const DsaProblem& problem,
                                                       const DsaSolution& solution);

// Starting from a valid placement, greedily moves complete colocation classes
// within their existing fixed pool to reduce physical reuse. This is an
// experimental sparse reference constructor, not an optimality claim.
[[nodiscard]] SparseReferenceResult BuildSparseReferencePlacement(
    const DsaProblem& problem, const DsaSolution& initial, std::size_t max_passes = 8);

}  // namespace dsa

#endif  // DSA_REUSE_GEOMETRY_H_

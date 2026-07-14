#ifndef DSA_DETAIL_PLACEMENT_ENGINE_H_
#define DSA_DETAIL_PLACEMENT_ENGINE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "dsa/model.h"

namespace dsa::detail {

// The ordering-search view of one colocation class. Hard address conflicts
// include temporal overlap and PyPTO-specific separation/exclusive-pin edges.
// Keeping this view next to the placement preparation code prevents search
// policies from reconstructing subtly different conflict graphs.
struct PlacementSearchNode {
  BufferId representative = 0;
  std::string name;
  std::uint64_t size = 0;
  PoolId pool = kDefaultPool;
  bool pinned = false;
  std::vector<BufferId> conflicts;
};

struct PlacementSearchSpace {
  std::vector<PlacementSearchNode> nodes;
  std::vector<std::string> errors;
  bool flexible_pools = false;
};

enum class PlacementStrategy {
  // Decreasing size with earliest-lifetime tie-breaking, then lowest-address
  // first fit. This is the historical dsa-solver initializer.
  kFirstFit,
  // OpenXLA GlobalDecreasingSizeBestFitHeap's spatial policy: decreasing size,
  // decreasing live-range duration, stable id; choose the smallest free chunk
  // that fits, breaking ties toward the lowest address.
  kXlaSpatialBestFit,
};

// Returns one representative id per colocation class in deterministic
// decreasing-size order.
[[nodiscard]] std::vector<BufferId> DefaultPlacementOrder(const DsaProblem& problem);

// Builds the graph searched by ordering-based solvers. Nodes are colocation
// classes, and edges are exactly the already-placed ranges that can block one
// another in PlaceWithOrder. Flexible pool assignment is reported explicitly
// because the current placement engine supports only fixed pools.
[[nodiscard]] PlacementSearchSpace BuildPlacementSearchSpace(const DsaProblem& problem);

// Runs the shared first-fit placement engine using the supplied class priority.
// Missing classes are appended in deterministic default order.
[[nodiscard]] DsaResult PlaceWithOrder(const DsaProblem& problem,
                                       const std::vector<BufferId>& priority,
                                       PlacementStrategy strategy = PlacementStrategy::kFirstFit);

}  // namespace dsa::detail

#endif  // DSA_DETAIL_PLACEMENT_ENGINE_H_

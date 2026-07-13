#ifndef DSA_DETAIL_PLACEMENT_ENGINE_H_
#define DSA_DETAIL_PLACEMENT_ENGINE_H_

#include <vector>

#include "dsa/model.h"

namespace dsa::detail {

// Returns one representative id per colocation class in deterministic
// decreasing-size order.
[[nodiscard]] std::vector<BufferId> DefaultPlacementOrder(const DsaProblem& problem);

// Runs the shared first-fit placement engine using the supplied class priority.
// Missing classes are appended in deterministic default order.
[[nodiscard]] DsaResult PlaceWithOrder(const DsaProblem& problem,
                                       const std::vector<BufferId>& priority);

}  // namespace dsa::detail

#endif  // DSA_DETAIL_PLACEMENT_ENGINE_H_

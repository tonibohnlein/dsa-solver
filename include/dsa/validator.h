#ifndef DSA_VALIDATOR_H_
#define DSA_VALIDATOR_H_

#include <string>
#include <vector>

#include "dsa/model.h"

namespace dsa {

[[nodiscard]] bool LifetimesOverlap(const Buffer& first, const Buffer& second) noexcept;
[[nodiscard]] bool HaveTemporalConflict(const DsaProblem& problem, const Buffer& first,
                                        const Buffer& second) noexcept;
[[nodiscard]] bool AddressRangesOverlap(std::uint64_t first_offset, std::uint64_t first_size,
                                        std::uint64_t second_offset,
                                        std::uint64_t second_size) noexcept;

// Structural validation independent of any solver.
[[nodiscard]] std::vector<std::string> ValidateProblem(const DsaProblem& problem);

// Recomputes every hard constraint against a candidate solution. An empty
// result means the solution is valid.
[[nodiscard]] std::vector<std::string> ValidateSolution(const DsaProblem& problem,
                                                        const DsaSolution& solution);

// Recomputes peak and optional reuse costs from a validated solution.
[[nodiscard]] ObjectiveValue EvaluateObjective(const DsaProblem& problem,
                                               const DsaSolution& solution);

// Evaluate one component of ObjectiveSpec from an already recomputed
// ObjectiveValue. Capacity overflow is derived from the problem's pool caps.
[[nodiscard]] std::uint64_t EvaluateObjectiveMetric(const DsaProblem& problem,
                                                    const ObjectiveValue& objective,
                                                    ObjectiveMetric metric) noexcept;

}  // namespace dsa

#endif  // DSA_VALIDATOR_H_

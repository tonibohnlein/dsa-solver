#ifndef DSA_MINIMALLOC_CSV_H_
#define DSA_MINIMALLOC_CSV_H_

#include <filesystem>
#include <iosfwd>
#include <optional>

#include "dsa/model/model.h"

namespace dsa {

struct MiniMallocDocument {
  DsaProblem problem;
  std::optional<DsaSolution> solution;
};

// Reads id,lower,upper,size and an optional offset column. Lifetimes are
// half-open [lower, upper). Numeric ids are assigned in row order while the
// original id remains Buffer::name.
[[nodiscard]] MiniMallocDocument ReadMiniMallocCsv(std::istream& input, PoolId pool = kDefaultPool);
[[nodiscard]] MiniMallocDocument ReadMiniMallocCsvFile(const std::filesystem::path& path,
                                                       PoolId pool = kDefaultPool);

// Emits MiniMalloc-compatible CSV. When solution is non-null, an offset column
// is included and every buffer must have a placement.
void WriteMiniMallocCsv(std::ostream& output, const DsaProblem& problem,
                        const DsaSolution* solution = nullptr);
void WriteMiniMallocCsvFile(const std::filesystem::path& path, const DsaProblem& problem,
                            const DsaSolution* solution = nullptr);

}  // namespace dsa

#endif  // DSA_MINIMALLOC_CSV_H_

#include "dsa/analysis/reuse_geometry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dsa/model/validator.h"

namespace dsa {
namespace {

class DisjointSet {
 public:
  explicit DisjointSet(std::size_t size) : parent_(size) {
    std::iota(parent_.begin(), parent_.end(), 0);
  }

  std::size_t Find(std::size_t value) {
    if (parent_[value] != value) parent_[value] = Find(parent_[value]);
    return parent_[value];
  }

  void Union(std::size_t first, std::size_t second) {
    first = Find(first);
    second = Find(second);
    if (first != second) parent_[second] = first;
  }

 private:
  std::vector<std::size_t> parent_;
};

struct AllocationNode {
  std::vector<std::size_t> members;
  PoolId pool = kDefaultPool;
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
  std::uint64_t alignment = 1;
  bool movable = true;
  std::optional<std::uint64_t> pinned_offset;
};

struct Score {
  std::uint64_t pairs = 0;
  std::uint64_t bytes = 0;

  [[nodiscard]] bool operator<(const Score& other) const noexcept {
    return std::tie(pairs, bytes) < std::tie(other.pairs, other.bytes);
  }
};

bool AddOverflows(std::uint64_t first, std::uint64_t second) {
  return first > std::numeric_limits<std::uint64_t>::max() - second;
}

std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment) {
  const std::uint64_t remainder = value % alignment;
  if (remainder == 0) return value;
  const std::uint64_t increment = alignment - remainder;
  return AddOverflows(value, increment) ? std::numeric_limits<std::uint64_t>::max()
                                        : value + increment;
}

std::uint64_t AlignDown(std::uint64_t value, std::uint64_t alignment) {
  return value - value % alignment;
}

std::uint64_t LeastCommonMultiple(std::uint64_t first, std::uint64_t second) {
  const std::uint64_t divisor = std::gcd(first, second);
  if (first / divisor > std::numeric_limits<std::uint64_t>::max() / second) {
    throw std::invalid_argument("colocation alignment least common multiple overflows");
  }
  return first / divisor * second;
}

std::uint64_t OverlapBytes(std::uint64_t first_offset, std::uint64_t first_size,
                           std::uint64_t second_offset, std::uint64_t second_size) {
  if (!AddressRangesOverlap(first_offset, first_size, second_offset, second_size)) return 0;
  const std::uint64_t first_end = first_offset + first_size;
  const std::uint64_t second_end = second_offset + second_size;
  return std::min(first_end, second_end) - std::max(first_offset, second_offset);
}

std::vector<AllocationNode> BuildNodes(const DsaProblem& problem, const DsaSolution& solution) {
  std::unordered_map<BufferId, std::size_t> indices;
  for (std::size_t index = 0; index < problem.buffers.size(); ++index) {
    indices.emplace(problem.buffers[index].id, index);
  }
  DisjointSet colocations(problem.buffers.size());
  for (const Colocation& colocation : problem.colocations) {
    colocations.Union(indices.at(colocation.first), indices.at(colocation.second));
  }

  std::map<std::size_t, std::vector<std::size_t>> members_by_root;
  for (std::size_t index = 0; index < problem.buffers.size(); ++index) {
    members_by_root[colocations.Find(index)].push_back(index);
  }

  std::unordered_map<BufferId, const PinnedAllocation*> pins;
  for (const PinnedAllocation& pin : problem.pinned_allocations) pins.emplace(pin.buffer, &pin);

  std::vector<AllocationNode> nodes;
  nodes.reserve(members_by_root.size());
  for (const auto& [root, members] : members_by_root) {
    static_cast<void>(root);
    AllocationNode node;
    node.members = members;
    const Buffer& first_buffer = problem.buffers[members.front()];
    const Placement* first_placement = solution.Find(first_buffer.id);
    if (first_placement == nullptr) throw std::invalid_argument("initial placement is incomplete");
    node.pool = first_placement->pool;
    node.offset = first_placement->offset;
    for (std::size_t member : members) {
      const Buffer& buffer = problem.buffers[member];
      const Placement* placement = solution.Find(buffer.id);
      if (placement == nullptr || placement->pool != node.pool || placement->offset != node.offset) {
        throw std::invalid_argument("colocation class does not share one placement");
      }
      node.size = std::max(node.size, buffer.size);
      node.alignment = LeastCommonMultiple(node.alignment, buffer.alignment);
      const auto pin = pins.find(buffer.id);
      if (pin != pins.end()) {
        node.movable = false;
        if (node.pinned_offset && *node.pinned_offset != pin->second->offset) {
          throw std::invalid_argument("colocation class has incompatible pinned offsets");
        }
        node.pinned_offset = pin->second->offset;
      }
    }
    nodes.push_back(std::move(node));
  }
  return nodes;
}

bool HasSeparation(const DsaProblem& problem, const AllocationNode& first,
                   const AllocationNode& second) {
  const auto in_node = [&](const AllocationNode& node, BufferId id) {
    return std::any_of(node.members.begin(), node.members.end(), [&](std::size_t member) {
      return problem.buffers[member].id == id;
    });
  };
  for (const Separation& separation : problem.separations) {
    if ((in_node(first, separation.first) && in_node(second, separation.second)) ||
        (in_node(first, separation.second) && in_node(second, separation.first))) {
      return true;
    }
  }
  return false;
}

bool HasExclusivePin(const DsaProblem& problem, const AllocationNode& node) {
  for (const PinnedAllocation& pin : problem.pinned_allocations) {
    if (!pin.exclusive_for_all_time) continue;
    for (std::size_t member : node.members) {
      if (problem.buffers[member].id == pin.buffer) return true;
    }
  }
  return false;
}

bool HaveTemporalConflict(const DsaProblem& problem, const AllocationNode& first,
                          const AllocationNode& second) {
  for (std::size_t first_member : first.members) {
    for (std::size_t second_member : second.members) {
      if (dsa::HaveTemporalConflict(problem, problem.buffers[first_member],
                                    problem.buffers[second_member])) {
        return true;
      }
    }
  }
  return false;
}

bool MustRemainDisjoint(const DsaProblem& problem, const AllocationNode& first,
                        const AllocationNode& second) {
  return HasExclusivePin(problem, first) || HasExclusivePin(problem, second) ||
         HasSeparation(problem, first, second) || HaveTemporalConflict(problem, first, second);
}

std::vector<std::vector<bool>> BuildMustRemainDisjoint(
    const DsaProblem& problem, const std::vector<AllocationNode>& nodes) {
  std::vector<std::vector<bool>> result(nodes.size(), std::vector<bool>(nodes.size(), false));
  for (std::size_t first = 0; first < nodes.size(); ++first) {
    for (std::size_t second = first + 1; second < nodes.size(); ++second) {
      const bool value = MustRemainDisjoint(problem, nodes[first], nodes[second]);
      result[first][second] = value;
      result[second][first] = value;
    }
  }
  return result;
}

Score PairScore(const AllocationNode& first, std::uint64_t first_offset,
                const AllocationNode& second, std::uint64_t second_offset,
                bool must_remain_disjoint) {
  if (first.pool != second.pool || must_remain_disjoint) return {};
  const std::uint64_t bytes =
      OverlapBytes(first_offset, first.size, second_offset, second.size);
  return {bytes == 0 ? 0U : 1U, bytes};
}

Score TotalScore(const std::vector<AllocationNode>& nodes,
                 const std::vector<std::vector<bool>>& must_remain_disjoint) {
  Score score;
  for (std::size_t first = 0; first < nodes.size(); ++first) {
    for (std::size_t second = first + 1; second < nodes.size(); ++second) {
      const Score pair = PairScore(nodes[first], nodes[first].offset, nodes[second],
                                   nodes[second].offset, must_remain_disjoint[first][second]);
      score.pairs += pair.pairs;
      score.bytes += pair.bytes;
    }
  }
  return score;
}

Score NodeScore(const std::vector<AllocationNode>& nodes,
                const std::vector<std::vector<bool>>& must_remain_disjoint,
                std::size_t node_index, std::uint64_t offset) {
  Score score;
  for (std::size_t other = 0; other < nodes.size(); ++other) {
    if (other == node_index) continue;
    const Score pair = PairScore(nodes[node_index], offset, nodes[other], nodes[other].offset,
                                 must_remain_disjoint[node_index][other]);
    score.pairs += pair.pairs;
    score.bytes += pair.bytes;
  }
  return score;
}

bool FitsAt(const DsaProblem& problem, const std::vector<AllocationNode>& nodes,
            const std::vector<std::vector<bool>>& must_remain_disjoint,
            std::size_t node_index, std::uint64_t offset) {
  const AllocationNode& node = nodes[node_index];
  if (offset % node.alignment != 0 || AddOverflows(offset, node.size)) return false;
  const Pool* pool = problem.FindPool(node.pool);
  if (pool == nullptr || !pool->capacity || offset + node.size > *pool->capacity) return false;
  for (const AddressRange& reserved : pool->reserved_ranges) {
    if (AddressRangesOverlap(offset, node.size, reserved.begin, reserved.end - reserved.begin)) {
      return false;
    }
  }
  for (std::size_t other = 0; other < nodes.size(); ++other) {
    if (other == node_index || nodes[other].pool != node.pool) continue;
    if (must_remain_disjoint[node_index][other] &&
        AddressRangesOverlap(offset, node.size, nodes[other].offset, nodes[other].size)) {
      return false;
    }
  }
  return true;
}

std::set<std::uint64_t> CandidateOffsets(const DsaProblem& problem,
                                         const std::vector<AllocationNode>& nodes,
                                         std::size_t node_index) {
  const AllocationNode& node = nodes[node_index];
  const Pool* pool = problem.FindPool(node.pool);
  std::set<std::uint64_t> candidates{node.offset, 0};
  if (pool == nullptr || !pool->capacity || node.size > *pool->capacity) return candidates;
  candidates.insert(AlignDown(*pool->capacity - node.size, node.alignment));
  for (std::size_t other = 0; other < nodes.size(); ++other) {
    if (other == node_index || nodes[other].pool != node.pool) continue;
    const AllocationNode& placed = nodes[other];
    if (!AddOverflows(placed.offset, placed.size)) {
      candidates.insert(AlignUp(placed.offset + placed.size, node.alignment));
    }
    if (placed.offset >= node.size) {
      candidates.insert(AlignDown(placed.offset - node.size, node.alignment));
    }
  }
  for (const AddressRange& reserved : pool->reserved_ranges) {
    candidates.insert(AlignUp(reserved.end, node.alignment));
    if (reserved.begin >= node.size) {
      candidates.insert(AlignDown(reserved.begin - node.size, node.alignment));
    }
  }
  return candidates;
}

DsaSolution NodesToSolution(const DsaProblem& problem, const std::vector<AllocationNode>& nodes) {
  DsaSolution solution;
  for (const AllocationNode& node : nodes) {
    for (std::size_t member : node.members) {
      solution.placements.emplace(problem.buffers[member].id, Placement{node.pool, node.offset});
    }
  }
  return solution;
}

}  // namespace

ReuseGeometryStats EvaluateReuseGeometry(const DsaProblem& problem,
                                         const DsaSolution& solution) {
  const std::vector<std::string> errors = ValidateSolution(problem, solution);
  if (!errors.empty()) throw std::invalid_argument("invalid placement: " + errors.front());
  const std::vector<AllocationNode> nodes = BuildNodes(problem, solution);
  const Score score = TotalScore(nodes, BuildMustRemainDisjoint(problem, nodes));
  return {score.pairs, score.bytes};
}

SparseReferenceResult BuildSparseReferencePlacement(const DsaProblem& problem,
                                                    const DsaSolution& initial,
                                                    std::size_t max_passes) {
  const std::vector<std::string> errors = ValidateSolution(problem, initial);
  if (!errors.empty()) throw std::invalid_argument("invalid initial placement: " + errors.front());
  std::vector<AllocationNode> nodes = BuildNodes(problem, initial);
  const std::vector<std::vector<bool>> must_remain_disjoint =
      BuildMustRemainDisjoint(problem, nodes);
  SparseReferenceResult result;
  const Score initial_score = TotalScore(nodes, must_remain_disjoint);
  result.initial = {initial_score.pairs, initial_score.bytes};

  for (std::size_t pass = 0; pass < max_passes; ++pass) {
    bool changed = false;
    std::vector<std::size_t> order(nodes.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t first, std::size_t second) {
      const Score first_score = NodeScore(nodes, must_remain_disjoint, first, nodes[first].offset);
      const Score second_score =
          NodeScore(nodes, must_remain_disjoint, second, nodes[second].offset);
      return std::tie(first_score.pairs, first_score.bytes, nodes[first].size) >
             std::tie(second_score.pairs, second_score.bytes, nodes[second].size);
    });
    for (std::size_t node_index : order) {
      AllocationNode& node = nodes[node_index];
      if (!node.movable) continue;
      const Score current = NodeScore(nodes, must_remain_disjoint, node_index, node.offset);
      Score best = current;
      std::uint64_t best_offset = node.offset;
      for (std::uint64_t candidate : CandidateOffsets(problem, nodes, node_index)) {
        if (!FitsAt(problem, nodes, must_remain_disjoint, node_index, candidate)) continue;
        const Score score = NodeScore(nodes, must_remain_disjoint, node_index, candidate);
        if (score < best || (!(best < score) && !(score < best) && candidate < best_offset)) {
          best = score;
          best_offset = candidate;
        }
      }
      if (best < current && best_offset != node.offset) {
        node.offset = best_offset;
        ++result.accepted_moves;
        changed = true;
      }
    }
    result.passes = pass + 1;
    if (!changed) break;
  }

  result.solution = NodesToSolution(problem, nodes);
  const std::vector<std::string> final_errors = ValidateSolution(problem, result.solution);
  if (!final_errors.empty()) {
    throw std::runtime_error("sparse reference produced an invalid placement: " +
                             final_errors.front());
  }
  const Score final_score = TotalScore(nodes, must_remain_disjoint);
  result.final = {final_score.pairs, final_score.bytes};
  return result;
}

}  // namespace dsa

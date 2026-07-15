# Apache TVM hill climb and the PyPTO refinement path

## Why it is a separate solver

The original `LocalSearchSolver` and Apache TVM USMP both search buffer orderings decoded by greedy
placement, but they use different neighborhoods:

- `LocalSearchSolver` samples arbitrary swaps, insertions, and range reversals, with restarts and
  perturbations after stagnation.
- `TvmHillClimbSolver` targets the interference graph around buffers that determine a pool's high-water
  mark. It is a named baseline so future changes to the research solver do not silently change what
  “TVM-style” means in benchmark results.

The primary implementation reference is Apache TVM's
[`hill_climb.cc` at v0.14.0](https://github.com/apache/tvm/blob/v0.14.0/src/tir/usmp/algo/hill_climb.cc),
introduced by [apache/tvm#9704](https://github.com/apache/tvm/pull/9704). The broader component boundary
comes from TVM's [Unified Static Memory Planning RFC](https://discuss.tvm.apache.org/t/rfc-unified-static-memory-planning/10099):
extract buffer sizes, conflicts, alignments, and pool candidates; run a pluggable planner; then write
pool offsets back into TIR.

## TVM search policy

The historical USMP implementation does the following:

1. Sort buffers by decreasing size, then decreasing conflict degree, then reverse name order.
2. Decode that permutation with greedy lowest-gap placement.
3. Find buffers whose end address equals a pool's current high-water mark.
4. Pick one such boundary buffer. Collect its earlier conflict neighbors (first level), then the earlier
   conflict neighbors of those nodes (second level).
5. Swap one first-level node with a different first- or usually second-level node.
6. Accept an improved total pool size. It may also accept a worse result with a percentage that decays
   with the attempt number.
7. Stop after 500 attempts or when the supplied memory-pressure target is reached.

This is better described as graph-guided ordering search with a small simulated-annealing component than
as direct hill climbing over offsets. The offsets are regenerated from scratch after every permutation.

## Deliberate differences in this implementation

The solver preserves the search state and neighborhood, but fixes the boundary around them:

- `std::mt19937_64` and an explicit seed replace platform-specific `rand_r`/`rand` behavior.
- The best placement seen is retained even after accepting a worse transition.
- The shared first-fit decoder applies the buffer being placed's alignment and independently validates
  every returned solution.
- Over-capacity candidates remain complete placements and are ranked through the requested objective;
  this lets a search move gradually toward feasibility.
- A target is optional (`--target-total-peak`). Without one, the full attempt budget is consumed.

These choices make benchmark runs deterministic and safe, but the result should be described as a
behavioral reimplementation rather than byte-for-byte TVM compatibility.

## PyPTO adaptation

The same policy runs on both benchmark families because only its graph and decoder are generalized:

- a node is a semantic colocation class rather than necessarily one source buffer;
- edges include temporal conflicts, explicit PyPTO separations, and exclusive pinned allocations;
- multi-interval liveness and temporal exclusions affect temporal conflict construction;
- fixed pools, alignment, reserved ranges, and pinned offsets are handled by placement;
- the score is the instance's lexicographic objective, so PyPTO reuse/synchronization penalties can be
  optimized after capacity.

Flexible pool assignment remains unsupported. It is a separate decision variable and should not be
hidden inside an ordering policy.

## Known limitations and next ablations

Ordering search cannot reach every useful placement: changing an offset is possible only indirectly by
changing the global greedy order. TVM's graph neighborhood can also stall when a high-water node has too
few earlier movable neighbors, especially with tiny graphs, pinned nodes, or a high-water mark determined
by a reserved range.

The next research solver should keep this implementation unchanged as an ablation and add, in order:

1. a mixed neighborhood that falls back to insertion/reversal when the TVM move is unavailable;
2. incremental conflict/placement evaluation rather than rebuilding and validating every candidate;
3. direct offset-to-gap and contiguous-region moves with local repair;
4. bounded ejection chains/backtracking when a move displaces conflicting buffers;
5. PyPTO-specific moves for alias classes, pipeline separation groups, and eventually buffering-depth
   decisions;
6. reproducible comparisons on `standard_dsa` against the exact
   [Google MiniMalloc](https://github.com/google/minimalloc) solver, and on
   `pypto_hard_v1` against independently validated compiler objectives.

For the broader state of the art, [Futureproof Static Memory Planning](https://arxiv.org/abs/2504.04874)
is especially useful: it frames TVM hill climb, MiniMalloc, production heuristics, and the newer boxing-
based `idealloc` on both fragmentation and scalability. That two-axis evaluation is the right model for
this benchmark framework; solution quality without timeout/robustness data is incomplete.

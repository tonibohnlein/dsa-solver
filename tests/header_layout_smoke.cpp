// Copyright 2026 DSA-Solver Contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/algorithms/cypress_relaxation_solver.h"
#include "dsa/algorithms/first_fit_solver.h"
#include "dsa/algorithms/local_search_solver.h"
#include "dsa/algorithms/pypto_structured_search_solver.h"
#include "dsa/algorithms/reuse_penalty_baseline_solvers.h"
#include "dsa/algorithms/reuse_penalty_exact_solvers.h"
#include "dsa/algorithms/reuse_penalty_local_search_solver.h"
#include "dsa/algorithms/reuse_penalty_portfolio_solvers.h"
#include "dsa/algorithms/reuse_penalty_treewidth_solver.h"
#include "dsa/algorithms/solver.h"
#include "dsa/algorithms/tvm_hill_climb_solver.h"
#include "dsa/algorithms/xla_heap_solver.h"
#include "dsa/architecture.h"
#include "dsa/cypress_relaxation_solver.h"
#include "dsa/first_fit_solver.h"
#include "dsa/io/minimalloc_csv.h"
#include "dsa/minimalloc_csv.h"
#include "dsa/model.h"
#include "dsa/model/architecture.h"
#include "dsa/model/model.h"
#include "dsa/model/structured_problem.h"
#include "dsa/model/validator.h"
#include "dsa/reuse_penalty_baseline_solvers.h"
#include "dsa/reuse_penalty_exact_solvers.h"
#include "dsa/reuse_penalty_portfolio_solvers.h"
#include "dsa/solver.h"
#include "dsa/structured_problem.h"
#include "dsa/validator.h"

int main() {
  dsa::DsaProblem problem;
  problem.pools.front().capacity = 16;
  problem.buffers.push_back({0, "buffer", 16, 1, {{0, 1}}, {dsa::kDefaultPool}});
  const dsa::DsaResult result = dsa::FirstFitSolver().Solve(problem);
  return result.status == dsa::SolveStatus::kFeasible ? 0 : 1;
}

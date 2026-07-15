# Copyright 2026 DSA-Solver Contributors
# SPDX-License-Identifier: Apache-2.0

set(BOUND_PROBLEM "${BINARY_DIR}/dsa-bind-smoke.json")
execute_process(
  COMMAND "${DSA_BIND}"
    --program "${SOURCE_DIR}/tests/data/pypto_unbound_program_v1.json"
    --architecture "${SOURCE_DIR}/benchmarks/architectures/ascend910b-v1.json"
    --output "${BOUND_PROBLEM}"
  RESULT_VARIABLE BIND_RESULT
  OUTPUT_VARIABLE BIND_OUTPUT
  ERROR_VARIABLE BIND_ERROR
)
if(NOT BIND_RESULT EQUAL 0)
  message(FATAL_ERROR "dsa-bind failed (${BIND_RESULT})\n${BIND_OUTPUT}\n${BIND_ERROR}")
endif()

file(READ "${BOUND_PROBLEM}" BOUND_JSON)
foreach(EXPECTED
    "\"architecture_id\": \"Ascend910B\""
    "\"program_fingerprint_fnv1a64\""
    "\"architecture_fingerprint_fnv1a64\""
    "\"capacity\": 188416"
    "\"capacity\": 131072")
  if(NOT BOUND_JSON MATCHES "${EXPECTED}")
    message(FATAL_ERROR "bound problem is missing expected content: ${EXPECTED}")
  endif()
endforeach()

execute_process(
  COMMAND "${DSA_BENCH}" --input "${BOUND_PROBLEM}" --solver first-fit
  RESULT_VARIABLE BENCH_RESULT
  OUTPUT_VARIABLE BENCH_OUTPUT
  ERROR_VARIABLE BENCH_ERROR
)
if(NOT BENCH_RESULT EQUAL 0)
  message(FATAL_ERROR
    "dsa-bench rejected the bound problem (${BENCH_RESULT})\n${BENCH_OUTPUT}\n${BENCH_ERROR}"
  )
endif()
if(NOT BENCH_OUTPUT MATCHES "\"status\":\"feasible\"")
  message(FATAL_ERROR "bound problem did not solve feasibly\n${BENCH_OUTPUT}")
endif()

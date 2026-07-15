# Copyright 2026 DSA-Solver Contributors
# SPDX-License-Identifier: Apache-2.0

set(OUTPUT_DIR "${BINARY_DIR}/standard-suite-smoke")
file(REMOVE_RECURSE "${OUTPUT_DIR}")
execute_process(
  COMMAND "${DSA_SUITE}"
    --standard "${SOURCE_DIR}/tests/data/minimalloc_example.csv"
    --pypto "${SOURCE_DIR}/benchmarks/pypto-lib/examples/intermediate/layer_norm__pypto_layer_norm_rows.json"
    --output-dir "${OUTPUT_DIR}"
    --run-label standard-smoke
    --seeds 0,1
    --iterations 20
    --restarts 2
    --stagnation 10
    --deterministic-repetitions 2
    --minimalloc-timeout-ms 1000
    --standard-only
    --no-minimalloc
  RESULT_VARIABLE SUITE_RESULT
  OUTPUT_VARIABLE SUITE_OUTPUT
  ERROR_VARIABLE SUITE_ERROR
)
if(NOT SUITE_RESULT EQUAL 0)
  message(FATAL_ERROR
    "standard-only dsa-suite failed (${SUITE_RESULT})\n${SUITE_OUTPUT}\n${SUITE_ERROR}"
  )
endif()

foreach(REQUIRED results.jsonl summary.csv report.md)
  if(NOT EXISTS "${OUTPUT_DIR}/${REQUIRED}")
    message(FATAL_ERROR "standard-only suite did not write ${REQUIRED}")
  endif()
endforeach()
if(EXISTS "${OUTPUT_DIR}/features.csv")
  message(FATAL_ERROR "standard-only suite should not duplicate corpus features.csv")
endif()

file(SHA256 "${OUTPUT_DIR}/results.jsonl" RAW_RESULTS_BEFORE)
execute_process(
  COMMAND "${DSA_SUITE}"
    --standard "${SOURCE_DIR}/tests/data/minimalloc_example.csv"
    --pypto "${SOURCE_DIR}/benchmarks/pypto-lib/examples/intermediate/layer_norm__pypto_layer_norm_rows.json"
    --output-dir "${OUTPUT_DIR}"
    --run-label standard-smoke
    --seeds 0,1
    --iterations 20
    --restarts 2
    --stagnation 10
    --deterministic-repetitions 2
    --minimalloc-timeout-ms 1000
    --standard-only
    --report-only
    --no-minimalloc
  RESULT_VARIABLE REPORT_RESULT
  OUTPUT_VARIABLE REPORT_OUTPUT
  ERROR_VARIABLE REPORT_ERROR
)
if(NOT REPORT_RESULT EQUAL 0)
  message(FATAL_ERROR
    "standard-only report rebuild failed (${REPORT_RESULT})\n${REPORT_OUTPUT}\n${REPORT_ERROR}"
  )
endif()
if(REPORT_OUTPUT MATCHES "\\[suite\\]")
  message(FATAL_ERROR "report-only mode unexpectedly executed a solver")
endif()
file(SHA256 "${OUTPUT_DIR}/results.jsonl" RAW_RESULTS_AFTER)
if(NOT RAW_RESULTS_BEFORE STREQUAL RAW_RESULTS_AFTER)
  message(FATAL_ERROR "report-only mode modified raw benchmark results")
endif()

file(READ "${OUTPUT_DIR}/results.jsonl" RAW_RESULTS)
if(RAW_RESULTS MATCHES "\"profile\":\"pypto_")
  message(FATAL_ERROR "standard-only suite retained a structured PyPTO profile")
endif()
if(RAW_RESULTS MATCHES "\"capacity\":[0-9]")
  message(FATAL_ERROR "standard-only suite retained a pool capacity")
endif()
foreach(METHOD first_fit xla_heap tvm_hill_climb local_search)
  string(REGEX MATCHALL "\"method\":\"${METHOD}\"" METHOD_RUNS "${RAW_RESULTS}")
  list(LENGTH METHOD_RUNS METHOD_RUN_COUNT)
  if(NOT METHOD_RUN_COUNT EQUAL 4)
    message(FATAL_ERROR "expected four ${METHOD} repetitions, got ${METHOD_RUN_COUNT}")
  endif()
endforeach()

file(READ "${OUTPUT_DIR}/report.md" REPORT)
foreach(EXPECTED "## Solution quality" "## Runtime" "geometric-mean peak" "Overall" "--standard-only" "PyPTO")
  if(NOT REPORT MATCHES "${EXPECTED}")
    message(FATAL_ERROR "standard-only report is missing: ${EXPECTED}")
  endif()
endforeach()
if(REPORT MATCHES "PyPTO structured DSA")
  message(FATAL_ERROR "standard-only report contains the deferred PyPTO comparison")
endif()
if(REPORT MATCHES "\\| pypto-lib::")
  message(FATAL_ERROR "standard-only presentation leaked per-instance result rows")
endif()

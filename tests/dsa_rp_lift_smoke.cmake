if(NOT DEFINED DSA_RP_LIFT OR NOT DEFINED DSA_BENCH OR NOT DEFINED DSA_SUITE OR
   NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "dsa-rp-lift smoke test is missing a required path")
endif()

set(OUTPUT_DIR "${BINARY_DIR}/dsa-rp-lift-smoke")
file(REMOVE_RECURSE "${OUTPUT_DIR}")

execute_process(
  COMMAND "${DSA_RP_LIFT}"
    --input "${SOURCE_DIR}/tests/data/minimalloc_example.csv"
    --output "${OUTPUT_DIR}/corpus"
    --source-commit test-fixture
    --capacity 12
    --streams 2
    --seed 7
  RESULT_VARIABLE LIFT_RC
  OUTPUT_VARIABLE LIFT_OUT
  ERROR_VARIABLE LIFT_ERR
)
if(NOT LIFT_RC EQUAL 0)
  message(FATAL_ERROR "dsa-rp-lift failed:\n${LIFT_OUT}\n${LIFT_ERR}")
endif()

set(PROBLEM "${OUTPUT_DIR}/corpus/minimalloc_example.json")
if(NOT EXISTS "${PROBLEM}")
  message(FATAL_ERROR "dsa-rp-lift did not write ${PROBLEM}")
endif()
file(READ "${PROBLEM}" PROBLEM_JSON)
if(NOT PROBLEM_JSON MATCHES "\"profile\": \"dsa_rp_v1\"")
  message(FATAL_ERROR "lifted problem does not use dsa_rp_v1")
endif()
if(NOT PROBLEM_JSON MATCHES "\"dsa_rp_edge_construction\": \"maximal_access_happens_before_v1\"")
  message(FATAL_ERROR "lifted problem lost derivation provenance")
endif()

execute_process(
  COMMAND "${DSA_BENCH}"
    --input "${PROBLEM}"
    --solver canonical-greedy
  RESULT_VARIABLE BENCH_RC
  OUTPUT_VARIABLE BENCH_OUT
  ERROR_VARIABLE BENCH_ERR
)
if(NOT BENCH_RC EQUAL 0)
  message(FATAL_ERROR "dsa-bench rejected lifted problem:\n${BENCH_OUT}\n${BENCH_ERR}")
endif()

execute_process(
  COMMAND "${DSA_SUITE}"
    --dsa-rp "${OUTPUT_DIR}/corpus"
    --output-dir "${OUTPUT_DIR}/results"
    --seeds 7
    --iterations 100
    --restarts 1
    --no-minimalloc
    --no-core-relaxations
  RESULT_VARIABLE SUITE_RC
  OUTPUT_VARIABLE SUITE_OUT
  ERROR_VARIABLE SUITE_ERR
)
if(NOT SUITE_RC EQUAL 0)
  message(FATAL_ERROR "dsa-suite rejected lifted corpus:\n${SUITE_OUT}\n${SUITE_ERR}")
endif()
if(NOT EXISTS "${OUTPUT_DIR}/results/report.md")
  message(FATAL_ERROR "dsa-suite did not write a DSA-RP report")
endif()

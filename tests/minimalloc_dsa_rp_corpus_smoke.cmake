if(NOT DEFINED DSA_SUITE OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "MiniMalloc DSA-RP corpus smoke test is missing a required path")
endif()

set(CORPUS_DIR "${SOURCE_DIR}/benchmarks/minimalloc-dsa-rp")
file(GLOB CORPUS_FILES "${CORPUS_DIR}/*.json")
list(LENGTH CORPUS_FILES CORPUS_SIZE)
if(NOT CORPUS_SIZE EQUAL 11)
  message(FATAL_ERROR "expected 11 MiniMalloc DSA-RP instances, found ${CORPUS_SIZE}")
endif()

set(OUTPUT_DIR "${BINARY_DIR}/minimalloc-dsa-rp-corpus-smoke")
file(REMOVE_RECURSE "${OUTPUT_DIR}")
execute_process(
  COMMAND "${DSA_SUITE}"
    --dsa-rp "${CORPUS_DIR}"
    --output-dir "${OUTPUT_DIR}"
    --features-only
  RESULT_VARIABLE SUITE_RC
  OUTPUT_VARIABLE SUITE_OUT
  ERROR_VARIABLE SUITE_ERR
)
if(NOT SUITE_RC EQUAL 0)
  message(FATAL_ERROR "dsa-suite rejected the checked-in MiniMalloc DSA-RP corpus:\n"
                      "${SUITE_OUT}\n${SUITE_ERR}")
endif()
if(NOT EXISTS "${OUTPUT_DIR}/features.csv")
  message(FATAL_ERROR "dsa-suite did not summarize the MiniMalloc DSA-RP corpus")
endif()

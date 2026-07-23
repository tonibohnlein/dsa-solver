# Copyright 2026 dsa-solver contributors
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED DSA_CORPUS OR NOT DEFINED DSA_BENCH OR NOT DEFINED DSA_SUITE OR
   NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "DSA-RP corpus smoke test is missing a required path")
endif()

set(INPUT_DIR "${BINARY_DIR}/dsa-rp-corpus-input")
set(OUTPUT_DIR "${BINARY_DIR}/dsa-rp-corpus-output")
file(REMOVE_RECURSE "${INPUT_DIR}" "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${INPUT_DIR}/case_a" "${INPUT_DIR}/case_b")
file(COPY
  "${SOURCE_DIR}/tests/data/recognized_cross_pipe_v1.json"
  DESTINATION "${INPUT_DIR}/case_a"
)
file(RENAME
  "${INPUT_DIR}/case_a/recognized_cross_pipe_v1.json"
  "${INPUT_DIR}/case_a/recognized_cross_pipe.dsa.json"
)
file(COPY
  "${SOURCE_DIR}/tests/data/pypto_structured_v1.json"
  DESTINATION "${INPUT_DIR}/case_b"
)
file(RENAME
  "${INPUT_DIR}/case_b/pypto_structured_v1.json"
  "${INPUT_DIR}/case_b/no_cross_pipe.dsa.json"
)

execute_process(
  COMMAND "${DSA_CORPUS}"
    --input "${INPUT_DIR}"
    --output "${OUTPUT_DIR}"
    --source-repo pypto
    --source-commit 0123456789abcdef
    --producer-repo pypto
    --producer-commit fedcba9876543210
    --namespace pypto
    --cross-pipe-variants
  RESULT_VARIABLE IMPORT_RESULT
  OUTPUT_VARIABLE IMPORT_OUTPUT
  ERROR_VARIABLE IMPORT_ERROR
)
if(NOT IMPORT_RESULT EQUAL 0)
  message(FATAL_ERROR
    "DSA-RP corpus import failed (${IMPORT_RESULT})\n${IMPORT_OUTPUT}\n${IMPORT_ERROR}"
  )
endif()

file(GLOB_RECURSE INSTANCES "${OUTPUT_DIR}/instances/*.json")
list(LENGTH INSTANCES INSTANCE_COUNT)
if(NOT INSTANCE_COUNT EQUAL 2)
  message(FATAL_ERROR "expected paired hard/soft DSA-RP instances, found ${INSTANCE_COUNT}")
endif()

file(GLOB_RECURSE HARD_INSTANCES "${OUTPUT_DIR}/instances/*-cross-pipe-hard.json")
file(GLOB_RECURSE SOFT_INSTANCES "${OUTPUT_DIR}/instances/*-cross-pipe-soft.json")
list(LENGTH HARD_INSTANCES HARD_COUNT)
list(LENGTH SOFT_INSTANCES SOFT_COUNT)
if(NOT HARD_COUNT EQUAL 1 OR NOT SOFT_COUNT EQUAL 1)
  message(FATAL_ERROR "DSA-RP corpus did not name the hard/soft pair deterministically")
endif()

file(STRINGS "${OUTPUT_DIR}/manifest.tsv" MANIFEST_ROWS)
list(LENGTH MANIFEST_ROWS MANIFEST_ROW_COUNT)
if(NOT MANIFEST_ROW_COUNT EQUAL 4)
  message(FATAL_ERROR
    "expected a header, one skipped source, and one hard/soft pair in the manifest")
endif()
file(READ "${OUTPUT_DIR}/manifest.tsv" MANIFEST_TEXT)
string(FIND "${MANIFEST_TEXT}" "no_cross_pipe_edges" SKIPPED_SOURCE)
if(SKIPPED_SOURCE EQUAL -1)
  message(FATAL_ERROR "edge-free DSA-RP source was not recorded as skipped")
endif()
list(GET HARD_INSTANCES 0 HARD_INSTANCE)
list(GET SOFT_INSTANCES 0 SOFT_INSTANCE)
file(READ "${HARD_INSTANCE}" HARD_TEXT)
file(READ "${SOFT_INSTANCE}" SOFT_TEXT)
foreach(EXPECTED
    "\"dsa_rp_edge_policy\": \"hard_v1\""
    "\"reasons\": ["
    "\"cross_pipe\"")
  string(FIND "${HARD_TEXT}" "${EXPECTED}" FOUND)
  if(FOUND EQUAL -1)
    message(FATAL_ERROR "hard DSA-RP variant is missing '${EXPECTED}'")
  endif()
endforeach()
foreach(EXPECTED
    "\"dsa_rp_edge_policy\": \"soft_v1\""
    "\"reuse_penalties\": ["
    "\"reason\": \"cross_pipe\"")
  string(FIND "${SOFT_TEXT}" "${EXPECTED}" FOUND)
  if(FOUND EQUAL -1)
    message(FATAL_ERROR "soft DSA-RP variant is missing '${EXPECTED}'")
  endif()
endforeach()

foreach(INSTANCE IN ITEMS "${HARD_INSTANCE}" "${SOFT_INSTANCE}")
  execute_process(
    COMMAND "${DSA_BENCH}" --input "${INSTANCE}" --solver first-fit
    RESULT_VARIABLE BENCH_RESULT
    OUTPUT_VARIABLE BENCH_OUTPUT
    ERROR_VARIABLE BENCH_ERROR
  )
  if(NOT BENCH_RESULT EQUAL 0)
    message(FATAL_ERROR
      "DSA-RP corpus instance did not replay (${BENCH_RESULT})\n${BENCH_OUTPUT}\n${BENCH_ERROR}"
    )
  endif()
endforeach()

execute_process(
  COMMAND "${DSA_SUITE}"
    --pypto "${OUTPUT_DIR}/instances"
    --output-dir "${OUTPUT_DIR}/features"
    --dsa-rp-variants-only
    --features-only
  RESULT_VARIABLE SUITE_RESULT
  OUTPUT_VARIABLE SUITE_OUTPUT
  ERROR_VARIABLE SUITE_ERROR
)
if(NOT SUITE_RESULT EQUAL 0)
  message(FATAL_ERROR
    "DSA-RP-only suite filtering failed (${SUITE_RESULT})\n${SUITE_OUTPUT}\n${SUITE_ERROR}"
  )
endif()
file(STRINGS "${OUTPUT_DIR}/features/features.csv" FEATURE_ROWS)
list(LENGTH FEATURE_ROWS FEATURE_ROW_COUNT)
if(NOT FEATURE_ROW_COUNT EQUAL 3)
  message(FATAL_ERROR
    "DSA-RP-only suite filtering expected a header and two variants, found ${FEATURE_ROW_COUNT}")
endif()

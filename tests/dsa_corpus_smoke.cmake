# Copyright 2026 dsa-solver contributors
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED DSA_CORPUS OR NOT DEFINED DSA_BENCH OR NOT DEFINED DSA_SUITE OR
   NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "dsa corpus smoke test is missing a required path")
endif()

set(INPUT_DIR "${BINARY_DIR}/corpus-import-input")
set(OUTPUT_DIR "${BINARY_DIR}/corpus-import-output")
file(REMOVE_RECURSE "${INPUT_DIR}" "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${INPUT_DIR}/case_a" "${INPUT_DIR}/case_b" "${INPUT_DIR}/case_c")
file(COPY
  "${SOURCE_DIR}/benchmarks/pypto/pipeline_stage_separation_v1.json"
  DESTINATION "${INPUT_DIR}/case_a"
)
file(RENAME
  "${INPUT_DIR}/case_a/pipeline_stage_separation_v1.json"
  "${INPUT_DIR}/case_a/pypto_pipeline_stage_separation.dsa.json"
)
file(COPY
  "${SOURCE_DIR}/benchmarks/pypto/pipeline_stage_separation_v1.json"
  DESTINATION "${INPUT_DIR}/case_b"
)
file(RENAME
  "${INPUT_DIR}/case_b/pipeline_stage_separation_v1.json"
  "${INPUT_DIR}/case_b/pypto_duplicate_shape.dsa.json"
)
set(DUPLICATE_INPUT "${INPUT_DIR}/case_b/pypto_duplicate_shape.dsa.json")
file(READ "${DUPLICATE_INPUT}" DUPLICATE_TEXT)
string(REPLACE "mem_vec_3" "renamed_buffer_3" DUPLICATE_TEXT "${DUPLICATE_TEXT}")
string(REPLACE "mem_vec_4" "renamed_buffer_4" DUPLICATE_TEXT "${DUPLICATE_TEXT}")
string(REPLACE "stage_0" "renamed_alias_0" DUPLICATE_TEXT "${DUPLICATE_TEXT}")
string(REPLACE "stage_1" "renamed_alias_1" DUPLICATE_TEXT "${DUPLICATE_TEXT}")
string(REPLACE "\"name\": \"Vec\"" "\"name\": \"RenamedVec\"" DUPLICATE_TEXT
  "${DUPLICATE_TEXT}")
file(WRITE "${DUPLICATE_INPUT}" "${DUPLICATE_TEXT}")
file(COPY
  "${SOURCE_DIR}/benchmarks/pypto/chain_read_before_write_v1.json"
  DESTINATION "${INPUT_DIR}/case_c"
)
file(RENAME
  "${INPUT_DIR}/case_c/chain_read_before_write_v1.json"
  "${INPUT_DIR}/case_c/pypto_trivial_chain.dsa.json"
)

execute_process(
  COMMAND "${DSA_CORPUS}"
    --input "${INPUT_DIR}"
    --output "${OUTPUT_DIR}"
    --coverage-targets "${SOURCE_DIR}/tests/data/corpus_targets.tsv"
    --source-repo pypto-lib
    --source-commit 0123456789abcdef
    --producer-repo pypto
    --producer-commit fedcba9876543210
    --namespace pypto-lib
  RESULT_VARIABLE IMPORT_RESULT
  OUTPUT_VARIABLE IMPORT_OUTPUT
  ERROR_VARIABLE IMPORT_ERROR
)
if(NOT IMPORT_RESULT EQUAL 0)
  message(FATAL_ERROR
    "dsa-corpus import failed (${IMPORT_RESULT})\n${IMPORT_OUTPUT}\n${IMPORT_ERROR}"
  )
endif()

set(DOCUMENT
  "${OUTPUT_DIR}/documents/models/example/case_a/pypto_pipeline_stage_separation.json"
)
if(NOT EXISTS "${DOCUMENT}" OR
   NOT EXISTS "${OUTPUT_DIR}/manifest.tsv" OR
   NOT EXISTS "${OUTPUT_DIR}/coverage.tsv")
  message(FATAL_ERROR "dsa-corpus did not write its normalized document and indexes")
endif()

file(READ "${DOCUMENT}" DOCUMENT_TEXT)
foreach(EXPECTED
    "pypto-lib::models::example::case_a::pypto_pipeline_stage_separation"
    "corpus_source_commit"
    "corpus_producer_commit"
    "corpus_original_instance"
    "corpus_problem_fingerprint_fnv1a64"
    "corpus_source_fingerprint_fnv1a64"
    "mem_vec_3")
  string(FIND "${DOCUMENT_TEXT}" "${EXPECTED}" FOUND)
  if(FOUND EQUAL -1)
    message(FATAL_ERROR "normalized document is missing '${EXPECTED}'")
  endif()
endforeach()

file(GLOB_RECURSE NORMALIZED_DOCUMENTS "${OUTPUT_DIR}/documents/*.json")
list(LENGTH NORMALIZED_DOCUMENTS NORMALIZED_DOCUMENT_COUNT)
if(NOT NORMALIZED_DOCUMENT_COUNT EQUAL 1)
  message(FATAL_ERROR
    "dsa-corpus should deduplicate two identical observations, found ${NORMALIZED_DOCUMENT_COUNT} documents"
  )
endif()
file(READ "${OUTPUT_DIR}/manifest.tsv" MANIFEST_TEXT)
foreach(EXPECTED
    "selected\tselection_reason"
    "pipeline_structure"
    "trivial_no_placement_choice")
  string(FIND "${MANIFEST_TEXT}" "${EXPECTED}" FOUND)
  if(FOUND EQUAL -1)
    message(FATAL_ERROR "corpus manifest is missing '${EXPECTED}'")
  endif()
endforeach()
string(REGEX MATCHALL "\n[^\n]+" MANIFEST_ROWS "${MANIFEST_TEXT}")
list(LENGTH MANIFEST_ROWS MANIFEST_ROW_COUNT)
if(NOT MANIFEST_ROW_COUNT EQUAL 3)
  message(FATAL_ERROR "dsa-corpus manifest should retain all three source observations")
endif()
file(READ "${OUTPUT_DIR}/coverage.tsv" COVERAGE_TEXT)
foreach(EXPECTED
    "eligibility\treason"
    "case_external\tmodels/example/external.py\texample\t1\t0\texclude\textern_only_no_incore_dsa\t0\texcluded")
  string(FIND "${COVERAGE_TEXT}" "${EXPECTED}" FOUND)
  if(FOUND EQUAL -1)
    message(FATAL_ERROR "corpus coverage is missing '${EXPECTED}'")
  endif()
endforeach()

execute_process(
  COMMAND "${DSA_BENCH}" --input "${DOCUMENT}" --solver first-fit
  RESULT_VARIABLE BENCH_RESULT
  OUTPUT_VARIABLE BENCH_OUTPUT
  ERROR_VARIABLE BENCH_ERROR
)
if(NOT BENCH_RESULT EQUAL 0)
  message(FATAL_ERROR
    "normalized corpus document did not replay (${BENCH_RESULT})\n${BENCH_OUTPUT}\n${BENCH_ERROR}"
  )
endif()

execute_process(
  COMMAND "${DSA_SUITE}"
    --standard "${SOURCE_DIR}/benchmarks/standard/freed_region_subdivision_v1.json"
    --pypto "${OUTPUT_DIR}/documents"
    --output-dir "${OUTPUT_DIR}/suite-report"
    --run-label corpus-smoke
    --standard-capacity 12
    --seeds 0
    --iterations 10
    --restarts 1
    --no-minimalloc
    --no-core-relaxations
  RESULT_VARIABLE SUITE_RESULT
  OUTPUT_VARIABLE SUITE_OUTPUT
  ERROR_VARIABLE SUITE_ERROR
)
if(NOT SUITE_RESULT EQUAL 0)
  message(FATAL_ERROR
    "normalized corpus suite failed (${SUITE_RESULT})\n${SUITE_OUTPUT}\n${SUITE_ERROR}"
  )
endif()
if(NOT EXISTS "${OUTPUT_DIR}/suite-report/features.csv")
  message(FATAL_ERROR "corpus suite did not write features.csv")
endif()
file(READ "${OUTPUT_DIR}/suite-report/features.csv" FEATURES_TEXT)
foreach(EXPECTED "whole_slot_reuse" "pipeline_separations")
  string(FIND "${FEATURES_TEXT}" "${EXPECTED}" FOUND)
  if(FOUND EQUAL -1)
    message(FATAL_ERROR "corpus feature report is missing '${EXPECTED}'")
  endif()
endforeach()
file(READ "${OUTPUT_DIR}/suite-report/summary.csv" SUMMARY_TEXT)
foreach(EXPECTED "corpus_family" "corpus_source_path" "models/example/case_a.py")
  string(FIND "${SUMMARY_TEXT}" "${EXPECTED}" FOUND)
  if(FOUND EQUAL -1)
    message(FATAL_ERROR "corpus suite summary is missing '${EXPECTED}'")
  endif()
endforeach()
file(READ "${OUTPUT_DIR}/suite-report/report.md" REPORT_TEXT)
string(FIND "${REPORT_TEXT}"
  "freed_region_subdivision | public standard | 3 | 100" JSON_CAPACITY_FOUND)
if(JSON_CAPACITY_FOUND EQUAL -1)
  message(FATAL_ERROR "standard CSV capacity override changed an explicit JSON capacity")
endif()

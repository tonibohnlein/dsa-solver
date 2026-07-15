# Copyright 2026 dsa-solver contributors
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "PyPTO corpus layout test is missing SOURCE_DIR")
endif()

set(PYPTO_ROOT "${SOURCE_DIR}/benchmarks/pypto")
set(PYPTO_LIB_ROOT "${SOURCE_DIR}/benchmarks/pypto-lib")

file(GLOB_RECURSE ALL_INSTANCES
  "${PYPTO_ROOT}/*.json"
  "${PYPTO_LIB_ROOT}/*.json"
)
list(LENGTH ALL_INSTANCES INSTANCE_COUNT)
if(NOT INSTANCE_COUNT EQUAL 452)
  message(FATAL_ERROR "expected 452 PyPTO instances, found ${INSTANCE_COUNT}")
endif()

set(CORPUS_TABLE "${SOURCE_DIR}/benchmarks/corpus.csv")
if(NOT EXISTS "${CORPUS_TABLE}")
  message(FATAL_ERROR "PyPTO corpus statistics table is missing")
endif()
file(STRINGS "${CORPUS_TABLE}" CORPUS_TABLE_ROWS)
list(LENGTH CORPUS_TABLE_ROWS CORPUS_TABLE_ROW_COUNT)
if(NOT CORPUS_TABLE_ROW_COUNT EQUAL 453)
  message(FATAL_ERROR
    "expected a header plus 452 corpus statistics rows, found ${CORPUS_TABLE_ROW_COUNT}")
endif()
list(GET CORPUS_TABLE_ROWS 0 CORPUS_TABLE_HEADER)
foreach(REQUIRED_COLUMN
    "buffers"
    "memory_spaces"
    "pool_capacities_bytes"
    "min_buffer_bytes"
    "max_buffer_bytes"
    "uniform_buffer_size"
    "temporal_conflicts"
    "max_live_capacity_ratio"
    "alias_classes"
    "pipeline_groups")
  string(FIND "${CORPUS_TABLE_HEADER}" "${REQUIRED_COLUMN}" COLUMN_INDEX)
  if(COLUMN_INDEX EQUAL -1)
    message(FATAL_ERROR "corpus statistics table is missing '${REQUIRED_COLUMN}'")
  endif()
endforeach()

set(FAMILIES
  "${PYPTO_LIB_ROOT}/examples"
  "${PYPTO_LIB_ROOT}/models/deepseek"
  "${PYPTO_LIB_ROOT}/models/qwen3"
  "${PYPTO_ROOT}/system-tests"
  "${PYPTO_ROOT}/unit-tests"
)
set(EXPECTED_COUNTS 11 163 113 161 4)
foreach(FAMILY COUNT IN ZIP_LISTS FAMILIES EXPECTED_COUNTS)
  file(GLOB_RECURSE FAMILY_INSTANCES "${FAMILY}/*.json")
  list(LENGTH FAMILY_INSTANCES FAMILY_COUNT)
  if(NOT FAMILY_COUNT EQUAL COUNT)
    message(FATAL_ERROR "expected ${COUNT} instances under ${FAMILY}, found ${FAMILY_COUNT}")
  endif()
endforeach()

set(INSTANCE_DIRECTORIES)
foreach(INSTANCE IN LISTS ALL_INSTANCES)
  cmake_path(GET INSTANCE PARENT_PATH INSTANCE_DIRECTORY)
  list(APPEND INSTANCE_DIRECTORIES "${INSTANCE_DIRECTORY}")
endforeach()
list(REMOVE_DUPLICATES INSTANCE_DIRECTORIES)
foreach(INSTANCE_DIRECTORY IN LISTS INSTANCE_DIRECTORIES)
  file(GLOB DIRECT_INSTANCES "${INSTANCE_DIRECTORY}/*.json")
  file(GLOB CHILDREN LIST_DIRECTORIES true "${INSTANCE_DIRECTORY}/*")
  set(CHILD_DIRECTORY_COUNT 0)
  foreach(CHILD IN LISTS CHILDREN)
    if(IS_DIRECTORY "${CHILD}")
      math(EXPR CHILD_DIRECTORY_COUNT "${CHILD_DIRECTORY_COUNT} + 1")
    endif()
  endforeach()
  list(LENGTH DIRECT_INSTANCES DIRECT_INSTANCE_COUNT)
  if(DIRECT_INSTANCE_COUNT EQUAL 1 AND CHILD_DIRECTORY_COUNT EQUAL 0)
    message(FATAL_ERROR
      "single-instance leaf directory must be flattened: ${INSTANCE_DIRECTORY}")
  endif()
endforeach()

set(PROBLEM_FINGERPRINTS)
foreach(INSTANCE IN LISTS ALL_INSTANCES)
  file(READ "${INSTANCE}" JSON_TEXT)
  if(NOT JSON_TEXT MATCHES "\"schema_version\"[ \t\r\n]*:[ \t\r\n]*1")
    message(FATAL_ERROR "instance is not schema version 1: ${INSTANCE}")
  endif()
  if(INSTANCE MATCHES "/pypto/unit-tests/")
    continue()
  endif()
  if(NOT JSON_TEXT MATCHES
      "\"corpus_problem_fingerprint_fnv1a64\"[ \t\r\n]*:[ \t\r\n]*\"([0-9a-f]+)\"")
    message(FATAL_ERROR "normalized instance has no canonical problem fingerprint: ${INSTANCE}")
  endif()
  set(FINGERPRINT "${CMAKE_MATCH_1}")
  string(LENGTH "${FINGERPRINT}" FINGERPRINT_LENGTH)
  if(NOT FINGERPRINT_LENGTH EQUAL 16)
    message(FATAL_ERROR "canonical problem fingerprint must contain 16 hex digits: ${INSTANCE}")
  endif()
  list(FIND PROBLEM_FINGERPRINTS "${FINGERPRINT}" DUPLICATE_INDEX)
  if(NOT DUPLICATE_INDEX EQUAL -1)
    message(FATAL_ERROR "duplicate canonical problem fingerprint ${FINGERPRINT}: ${INSTANCE}")
  endif()
  list(APPEND PROBLEM_FINGERPRINTS "${FINGERPRINT}")
endforeach()

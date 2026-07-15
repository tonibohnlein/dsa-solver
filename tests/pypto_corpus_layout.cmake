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
if(NOT INSTANCE_COUNT EQUAL 478)
  message(FATAL_ERROR "expected 478 PyPTO instances, found ${INSTANCE_COUNT}")
endif()

set(FAMILIES
  "${PYPTO_LIB_ROOT}/examples"
  "${PYPTO_LIB_ROOT}/models/deepseek"
  "${PYPTO_LIB_ROOT}/models/qwen3"
  "${PYPTO_ROOT}/system-tests"
  "${PYPTO_ROOT}/unit-tests"
)
set(EXPECTED_COUNTS 11 166 117 179 5)
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

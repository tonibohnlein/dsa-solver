# Copyright 2026 dsa-solver contributors
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "PyPTO corpus layout test is missing SOURCE_DIR")
endif()

set(CORPUS_ROOT "${SOURCE_DIR}/benchmarks/pypto")
set(INSTANCE_ROOT "${CORPUS_ROOT}/instances")

file(GLOB ROOT_JSON "${CORPUS_ROOT}/*.json")
if(ROOT_JSON)
  message(FATAL_ERROR "PyPTO benchmark JSON must live below instances/")
endif()

file(GLOB_RECURSE ALL_INSTANCES "${INSTANCE_ROOT}/*.json")
list(LENGTH ALL_INSTANCES INSTANCE_COUNT)
if(NOT INSTANCE_COUNT EQUAL 478)
  message(FATAL_ERROR "expected 478 PyPTO instances, found ${INSTANCE_COUNT}")
endif()

set(FAMILIES
  "pypto-lib/examples"
  "pypto-lib/models/deepseek"
  "pypto-lib/models/qwen3"
  "pypto/system-tests"
  "pypto/unit-tests"
)
set(EXPECTED_COUNTS 11 166 117 179 5)
foreach(FAMILY COUNT IN ZIP_LISTS FAMILIES EXPECTED_COUNTS)
  file(GLOB_RECURSE FAMILY_INSTANCES "${INSTANCE_ROOT}/${FAMILY}/*.json")
  list(LENGTH FAMILY_INSTANCES FAMILY_COUNT)
  if(NOT FAMILY_COUNT EQUAL COUNT)
    message(FATAL_ERROR "expected ${COUNT} instances under ${FAMILY}, found ${FAMILY_COUNT}")
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

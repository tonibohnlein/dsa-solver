cmake_minimum_required(VERSION 3.20)

project(dsa_solver VERSION 0.6.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(DSA_NLOHMANN_JSON_INCLUDE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/single_include"
)
if(NOT EXISTS "${DSA_NLOHMANN_JSON_INCLUDE_DIR}/nlohmann/json.hpp")
  message(FATAL_ERROR
    "nlohmann/json submodule is missing; run: git submodule update --init third_party/json"
  )
endif()

add_library(dsa_solver
  src/dsa/model/model.cpp
  src/dsa/model/model.h
  src/dsa/model/structured_json.cpp
  src/dsa/model/structured_problem.cpp
  src/dsa/model/structured_problem.h
  src/dsa/model/validator.cpp
  src/dsa/model/validator.h
  src/dsa/algorithms/first_fit_solver.cpp
  src/dsa/algorithms/first_fit_solver.h
  src/dsa/algorithms/local_search_solver.cpp
  src/dsa/algorithms/local_search_solver.h
  src/dsa/algorithms/placement_engine.cpp
  src/dsa/algorithms/placement_engine.h
  src/dsa/algorithms/pypto_structured_search_solver.cpp
  src/dsa/algorithms/pypto_structured_search_solver.h
  src/dsa/algorithms/solver.cpp
  src/dsa/algorithms/solver.h
  src/dsa/algorithms/tvm_hill_climb_solver.cpp
  src/dsa/algorithms/tvm_hill_climb_solver.h
  src/dsa/algorithms/xla_heap_solver.cpp
  src/dsa/algorithms/xla_heap_solver.h
  src/dsa/io/minimalloc_csv.cpp
  src/dsa/io/minimalloc_csv.h
)
add_library(dsa::solver ALIAS dsa_solver)
set_target_properties(dsa_solver PROPERTIES
  CXX_EXTENSIONS OFF
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  EXPORT_NAME solver
  POSITION_INDEPENDENT_CODE ON
)
target_include_directories(dsa_solver
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${DSA_NLOHMANN_JSON_INCLUDE_DIR}
)

# Keep the original flat dsa/*.h include paths available to existing consumers
# while the source tree and preferred API use dsa/{model,algorithms,io}/*.h.
set(DSA_COMPATIBILITY_HEADERS
  src/dsa/model/model.h
  src/dsa/model/structured_problem.h
  src/dsa/model/validator.h
  src/dsa/algorithms/first_fit_solver.h
  src/dsa/algorithms/local_search_solver.h
  src/dsa/algorithms/pypto_structured_search_solver.h
  src/dsa/algorithms/solver.h
  src/dsa/algorithms/tvm_hill_climb_solver.h
  src/dsa/algorithms/xla_heap_solver.h
  src/dsa/io/minimalloc_csv.h
)
foreach(HEADER IN LISTS DSA_COMPATIBILITY_HEADERS)
  get_filename_component(HEADER_NAME "${HEADER}" NAME)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/${HEADER}"
    "${CMAKE_CURRENT_BINARY_DIR}/include/dsa/${HEADER_NAME}"
    COPYONLY
  )
endforeach()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(dsa_solver PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
  target_compile_options(dsa_solver PRIVATE /W4 /permissive-)
endif()

add_executable(dsa-bench tools/dsa_bench.cpp)
target_link_libraries(dsa-bench PRIVATE dsa::solver)
set_target_properties(dsa-bench PROPERTIES
  CXX_EXTENSIONS OFF
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(dsa-bench PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
  target_compile_options(dsa-bench PRIVATE /W4 /permissive-)
endif()

add_executable(dsa-corpus tools/dsa_corpus.cpp)
target_link_libraries(dsa-corpus PRIVATE dsa::solver)
set_target_properties(dsa-corpus PROPERTIES
  CXX_EXTENSIONS OFF
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(dsa-corpus PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
  target_compile_options(dsa-corpus PRIVATE /W4 /permissive-)
endif()

option(DSA_ENABLE_MINIMALLOC_BASELINE
  "Build dsa-suite with the pinned Google MiniMalloc exact baseline"
  ON
)

if(DSA_ENABLE_MINIMALLOC_BASELINE)
  set(DSA_MINIMALLOC_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minimalloc"
  )
  if(NOT EXISTS "${DSA_MINIMALLOC_SOURCE_DIR}/src/solver.cc")
    message(FATAL_ERROR
      "MiniMalloc submodule is missing; run: git submodule update --init --recursive third_party/minimalloc"
    )
  endif()

  set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
  add_subdirectory(
    "${DSA_MINIMALLOC_SOURCE_DIR}/external/abseil-cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/third_party/abseil-cpp"
    EXCLUDE_FROM_ALL
  )

  add_library(dsa_minimalloc_baseline STATIC
    third_party/minimalloc/src/converter.cc
    third_party/minimalloc/src/minimalloc.cc
    third_party/minimalloc/src/solver.cc
    third_party/minimalloc/src/sweeper.cc
    third_party/minimalloc/src/validator.cc
  )
  set_target_properties(dsa_minimalloc_baseline PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON
  )
  target_include_directories(dsa_minimalloc_baseline PUBLIC
    "${DSA_MINIMALLOC_SOURCE_DIR}/src"
  )
  target_link_libraries(dsa_minimalloc_baseline PUBLIC
    absl::flags
    absl::statusor
  )
endif()

add_executable(dsa-suite tools/dsa_suite.cpp)
target_link_libraries(dsa-suite PRIVATE dsa::solver)
target_include_directories(dsa-suite PRIVATE ${DSA_NLOHMANN_JSON_INCLUDE_DIR})
if(DSA_ENABLE_MINIMALLOC_BASELINE)
  target_compile_definitions(dsa-suite PRIVATE DSA_HAVE_MINIMALLOC=1)
  target_include_directories(dsa-suite SYSTEM PRIVATE
    "${DSA_MINIMALLOC_SOURCE_DIR}/src"
    "${DSA_MINIMALLOC_SOURCE_DIR}/external/abseil-cpp"
  )
  target_link_libraries(dsa-suite PRIVATE
    dsa_minimalloc_baseline
    absl::log
    absl::time
  )
endif()
set_target_properties(dsa-suite PROPERTIES
  CXX_EXTENSIONS OFF
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(dsa-suite PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
  target_compile_options(dsa-suite PRIVATE /W4 /permissive-)
endif()

if(BUILD_TESTING)
  add_executable(dsa-tests tests/dsa_tests.cpp)
  target_link_libraries(dsa-tests PRIVATE dsa::solver)
  target_compile_definitions(dsa-tests PRIVATE
    DSA_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  set_target_properties(dsa-tests PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(dsa-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  elseif(MSVC)
    target_compile_options(dsa-tests PRIVATE /W4 /permissive-)
  endif()
  add_test(NAME dsa-unit-tests COMMAND dsa-tests)

  add_executable(dsa-header-layout-smoke tests/header_layout_smoke.cpp)
  target_link_libraries(dsa-header-layout-smoke PRIVATE dsa::solver)
  set_target_properties(dsa-header-layout-smoke PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(
      dsa-header-layout-smoke PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
  elseif(MSVC)
    target_compile_options(dsa-header-layout-smoke PRIVATE /W4 /permissive-)
  endif()
  add_test(NAME dsa-header-layout-smoke COMMAND dsa-header-layout-smoke)

  add_test(
    NAME dsa-cli-local-search
    COMMAND dsa-bench
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/minimalloc_example.csv
      --solver local-search
      --capacity 12
      --seed 7
      --iterations 2000
      --restarts 4
  )
  add_test(
    NAME dsa-cli-reference
    COMMAND dsa-bench
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/minimalloc_example.csv
      --solver first-fit
      --reference-output ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/minimalloc_example_solution.csv
  )
  add_test(
    NAME dsa-cli-tvm-hill-climb
    COMMAND dsa-bench
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/minimalloc_example.csv
      --solver tvm-hill-climb
      --capacity 12
      --seed 7
      --iterations 500
      --target-total-peak 12
  )
  set_tests_properties(dsa-cli-tvm-hill-climb PROPERTIES
    PASS_REGULAR_EXPRESSION "\"search_budget\":500.*\"target_total_peak\":12"
  )
  add_test(
    NAME dsa-cli-structured
    COMMAND dsa-bench
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/pypto_structured_v1.json
      --solver local-search
      --seed 7
      --iterations 200
      --restarts 2
  )
  add_test(
    NAME dsa-cli-tvm-structured
    COMMAND dsa-bench
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/pypto_structured_v1.json
      --solver tvm-hill-climb
      --seed 7
      --iterations 200
  )
  add_test(
    NAME dsa-cli-core-relaxation
    COMMAND dsa-bench
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/pypto_structured_v1.json
      --core-relaxation-pool 3
      --solver first-fit
  )
  add_test(
    NAME dsa-cli-capability-report
    COMMAND dsa-bench
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/pypto_structured_v1.json
      --solver first-fit
  )
  add_test(
    NAME dsa-cli-pypto-exported-corpus
    COMMAND dsa-bench
      --input ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/pypto/unit-tests/memory-planning/issue_1908_fragmentation_v1.json
      --solver first-fit
  )
  add_test(
    NAME dsa-cli-xla-heap
    COMMAND dsa-bench
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/minimalloc_example.csv
      --solver xla-heap
      --target-total-peak 12
  )
  add_test(
    NAME dsa-cli-pypto-structured-search
    COMMAND dsa-bench
      --input ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/pypto/unit-tests/memory-planning/pipeline_stage_separation_v1.json
      --solver pypto-structured-search
      --seed 7
      --iterations 100
      --restarts 2
  )
  add_test(
    NAME dsa-suite-smoke
    COMMAND dsa-suite
      --standard ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/minimalloc_example.csv
      --pypto ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/pypto_structured_v1.json
      --output-dir ${CMAKE_CURRENT_BINARY_DIR}/suite-smoke
      --standard-capacity 12
      --seeds 7
      --iterations 100
      --restarts 2
      --minimalloc-timeout-ms 5000
  )
  set_tests_properties(dsa-suite-smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "wrote .*report.md"
  )
  add_test(
    NAME dsa-corpus-import-smoke
    COMMAND ${CMAKE_COMMAND}
      -DDSA_CORPUS=$<TARGET_FILE:dsa-corpus>
      -DDSA_BENCH=$<TARGET_FILE:dsa-bench>
      -DDSA_SUITE=$<TARGET_FILE:dsa-suite>
      -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/dsa_corpus_smoke.cmake
  )
  add_test(
    NAME dsa-pypto-corpus-layout
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/pypto_corpus_layout.cmake
  )
endif()

install(TARGETS dsa_solver dsa-bench dsa-corpus dsa-suite
  EXPORT dsa-solver-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(
  DIRECTORY src/dsa/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dsa
  FILES_MATCHING
  PATTERN "*.h"
  PATTERN "placement_engine.h" EXCLUDE
)
install(
  FILES ${DSA_COMPATIBILITY_HEADERS}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dsa
)
install(
  FILES schemas/dsa-problem-v1.schema.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/dsa-solver/schemas
)
install(
  FILES LICENSE NOTICE THIRD_PARTY_NOTICES.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/dsa-solver
)
install(
  FILES third_party/json/LICENSE.MIT
  DESTINATION ${CMAKE_INSTALL_DATADIR}/dsa-solver
  RENAME LICENSE.nlohmann-json
)
if(DSA_ENABLE_MINIMALLOC_BASELINE)
  install(
    FILES third_party/minimalloc/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dsa-solver
    RENAME LICENSE.minimalloc
  )
  install(
    FILES third_party/minimalloc/external/abseil-cpp/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dsa-solver
    RENAME LICENSE.abseil-cpp
  )
endif()

configure_package_config_file(
  cmake/dsa-solver-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/dsa-solver-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dsa-solver
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/dsa-solver-config-version.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/dsa-solver-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/dsa-solver-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dsa-solver
)
install(
  EXPORT dsa-solver-targets
  FILE dsa-solver-targets.cmake
  NAMESPACE dsa::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dsa-solver
)

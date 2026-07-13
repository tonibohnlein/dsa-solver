cmake_minimum_required(VERSION 3.20)

project(dsa_solver VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(dsa_solver
  src/first_fit_solver.cpp
  src/local_search_solver.cpp
  src/minimalloc_csv.cpp
  src/model.cpp
  src/placement_engine.cpp
  src/validator.cpp
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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
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

if(BUILD_TESTING)
  add_executable(dsa-tests tests/dsa_tests.cpp)
  target_link_libraries(dsa-tests PRIVATE dsa::solver)
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
endif()

install(TARGETS dsa_solver dsa-bench
  EXPORT dsa-solver-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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

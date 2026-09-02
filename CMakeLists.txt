cmake_minimum_required(VERSION 3.24)
project(StateIndex VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STATEINDEX_BUILD_TESTS "Build State Index tests" ON)
option(STATEINDEX_BUILD_EXAMPLES "Build State Index examples" ON)
option(STATEINDEX_BUILD_BENCHMARKS "Build State Index benchmarks" ON)
option(STATEINDEX_ENABLE_SYNTHETIC_DISTRIBUTED "Enable synthetic distributed scenarios" ON)
option(STATEINDEX_ENABLE_CUDA_PROOF "Enable real CUDA proof" OFF)

add_library(stateindex STATIC
  src/authority.cpp
  src/index.cpp
  src/engine.cpp
)
target_include_directories(stateindex PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(stateindex PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(stateindex PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(stateindex PRIVATE -Wall -Wextra -Werror)
endif()

add_library(StateIndex::stateindex ALIAS stateindex)

# Multiprocess coordinator and worker executables.
if(MSVC)
  set(SI_SYSTEM_LIBS ws2_32)
else()
  set(SI_SYSTEM_LIBS "")
endif()

add_executable(state-index-coordinator src/coordinator_main.cpp)
target_link_libraries(state-index-coordinator PRIVATE stateindex ${SI_SYSTEM_LIBS})
if(MSVC)
  target_compile_options(state-index-coordinator PRIVATE /W4 /WX /permissive-)
endif()

add_executable(state-index-worker src/worker_main.cpp)
target_link_libraries(state-index-worker PRIVATE stateindex ${SI_SYSTEM_LIBS})
if(MSVC)
  target_compile_options(state-index-worker PRIVATE /W4 /WX /permissive-)
endif()

add_executable(state-index src/cli_main.cpp)
target_link_libraries(state-index PRIVATE stateindex)
if(MSVC)
  target_compile_options(state-index PRIVATE /W4 /WX /permissive-)
endif()

install(TARGETS state-index-coordinator state-index-worker state-index
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Tests
if(STATEINDEX_BUILD_TESTS)
  enable_testing()
  function(add_sitest name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE stateindex)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    if(MSVC)
      target_compile_options(${name} PRIVATE /W4 /WX /permissive-)
    else()
      target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  endfunction()

  add_sitest(test_identity)
  add_sitest(test_record_model)
  add_sitest(test_indexes)
  add_sitest(test_query)
  add_sitest(test_invalidation)
  add_sitest(test_persistence)
  add_sitest(test_property)
  add_sitest(test_concurrency)
  add_sitest(test_adversarial)
  add_sitest(test_protocol)

  # Multiprocess proof: needs the coordinator/worker executables and a directory.
  add_executable(test_multiprocess tests/test_multiprocess.cpp)
  target_link_libraries(test_multiprocess PRIVATE stateindex)
  target_include_directories(test_multiprocess PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  if(MSVC)
    target_compile_options(test_multiprocess PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(test_multiprocess PRIVATE -Wall -Wextra -Werror)
  endif()
  add_test(NAME test_multiprocess COMMAND test_multiprocess ${CMAKE_CURRENT_BINARY_DIR}
           WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

  # CUDA proof (optional; requires a CUDA toolkit + device).
  if(STATEINDEX_ENABLE_CUDA_PROOF)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    add_executable(test_cuda tests/test_cuda.cpp)
    target_link_libraries(test_cuda PRIVATE CUDA::cudart stateindex)
    target_include_directories(test_cuda PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    if(NOT MSVC)
      set_target_properties(test_cuda PROPERTIES CUDA_ARCHITECTURES 120)
    endif()
    add_test(NAME test_cuda COMMAND test_cuda WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  endif()
endif()

# Examples
if(STATEINDEX_BUILD_EXAMPLES)
  set(SI_EXAMPLES
    01_state_identity 02_exact_lookup 03_kind_lookup 04_multi_location 05_generation_supersession
    06_compatibility_filter 07_dependency_filter 08_locality_ranking 09_invalidation 10_tombstone
    11_historical_query 12_persistence_recovery 13_multiprocess_authority 14_cuda_location 15_explain_query)
  foreach(ex ${SI_EXAMPLES})
    add_executable(example_${ex} examples/${ex}.cpp)
    target_link_libraries(example_${ex} PRIVATE stateindex)
    target_include_directories(example_${ex} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(MSVC)
      target_compile_options(example_${ex} PRIVATE /W4 /WX /permissive-)
    else()
      target_compile_options(example_${ex} PRIVATE -Wall -Wextra -Werror)
    endif()
  endforeach()
endif()

# Benchmarks
if(STATEINDEX_BUILD_BENCHMARKS)
  add_executable(state-index-benchmark benchmarks/benchmark.cpp)
  target_link_libraries(state-index-benchmark PRIVATE stateindex)
  if(MSVC)
    target_compile_options(state-index-benchmark PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(state-index-benchmark PRIVATE -Wall -Wextra -Werror)
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS stateindex
  EXPORT StateIndexTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT StateIndexTargets
  FILE StateIndexTargets.cmake
  NAMESPACE StateIndex::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StateIndex
)
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/StateIndexConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/StateIndexConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StateIndex)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/StateIndexConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/StateIndexConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/StateIndexConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StateIndex)

# CPack packaging of install artifacts (ZIP / TGZ).
set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_PACKAGE_NAME "StateIndex")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
include(CPack)

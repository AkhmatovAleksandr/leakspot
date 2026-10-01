cmake_minimum_required(VERSION 3.25)
project(leakspot VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler optimization and warnings
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -O3
)

# Use mold linker if available
execute_process(
    COMMAND mold --version
    OUTPUT_VARIABLE MOLD_VERSION
    ERROR_QUIET
    RESULT_VARIABLE MOLD_FOUND
)
if(MOLD_FOUND EQUAL 0)
    message(STATUS "Using mold linker: ${MOLD_VERSION}")
    add_link_options("-fuse-ld=mold")
endif()

include_directories(include)

set(LEAKSPOT_SOURCES
    src/types.cpp
    src/cli.cpp
    src/sampler.cpp
    src/regression.cpp
    src/detector.cpp
    src/reporter.cpp
    src/supervisor.cpp
    src/main.cpp
)

add_executable(leakspot ${LEAKSPOT_SOURCES})

# Test workloads
add_executable(test_leaky_memory tests/leaky_memory.cpp)
add_executable(test_leaky_fds tests/leaky_fds.cpp)
add_executable(test_stable_workload tests/stable_workload.cpp)

install(TARGETS leakspot DESTINATION bin)

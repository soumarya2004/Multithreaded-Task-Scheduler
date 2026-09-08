include(FetchContent)
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.0
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(WARNING
        "Benchmarks are being configured in '${CMAKE_BUILD_TYPE}' mode. "
        "Timing results from a non-Release build do not reflect real "
        "performance (no optimization, and Debug-mode container overhead "
        "dominates). Reconfigure with -DCMAKE_BUILD_TYPE=Release before "
        "trusting any number this suite reports.")
endif()
add_executable(scheduler_benchmark scheduler_benchmark.cpp)
target_link_libraries(scheduler_benchmark PRIVATE scheduler benchmark::benchmark)
target_compile_features(scheduler_benchmark PRIVATE cxx_std_20)
cmake_minimum_required(VERSION 3.20)
project(multithreaded_task_scheduler LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(SCHEDULER_ENABLE_TSAN "Build with ThreadSanitizer instrumentation" OFF)
option(SCHEDULER_BUILD_TESTS "Build GoogleTest test suite" OFF)      
option(SCHEDULER_BUILD_BENCHMARKS "Build Google Benchmark suite" OFF)
option(SCHEDULER_BUILD_EXAMPLES "Build example programs" ON)
find_package(Threads REQUIRED)
add_library(scheduler_warnings INTERFACE)
target_compile_options(scheduler_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
)
add_library(scheduler_sanitizers INTERFACE)
if(SCHEDULER_ENABLE_TSAN)
    target_compile_options(scheduler_sanitizers INTERFACE -fsanitize=thread -g -O1)
    target_link_options(scheduler_sanitizers INTERFACE -fsanitize=thread)
endif()
add_library(scheduler
    src/thread_pool.cpp
    src/work_queue.cpp
)
target_include_directories(scheduler PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(scheduler
    PUBLIC Threads::Threads
    PRIVATE scheduler_warnings scheduler_sanitizers
)
target_compile_features(scheduler PUBLIC cxx_std_20)
if(SCHEDULER_BUILD_EXAMPLES)
    add_executable(basic_usage examples/basic_usage.cpp)
    target_link_libraries(basic_usage PRIVATE scheduler scheduler_warnings scheduler_sanitizers)
endif()
if(SCHEDULER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(SCHEDULER_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
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
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
)
add_library(scheduler_sanitizers INTERFACE)
if(SCHEDULER_ENABLE_TSAN)
    target_compile_options(scheduler_sanitizers INTERFACE
        -fsanitize=thread
        -g
        -O1
    )
    target_link_options(scheduler_sanitizers INTERFACE
        -fsanitize=thread
    )
endif()
add_library(scheduler
    src/thread_pool.cpp
    src/work_queue.cpp
    src/priority_queue.cpp
    src/scheduler.cpp
    src/worker.cpp
    src/work_stealing_pool.cpp
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
    add_executable(basic_usage
        examples/basic_usage.cpp
    )
    target_link_libraries(basic_usage
        PRIVATE scheduler scheduler_warnings scheduler_sanitizers
    )
    add_executable(priority_usage
        examples/priority_usage.cpp
    )
    target_link_libraries(priority_usage
        PRIVATE scheduler scheduler_warnings scheduler_sanitizers
    )
    add_executable(work_stealing_usage
        examples/work_stealing_usage.cpp
    )
    target_link_libraries(work_stealing_usage
        PRIVATE scheduler scheduler_warnings scheduler_sanitizers
    )
    add_executable(contention_comparison
        examples/contention_comparison.cpp
    )
    target_link_libraries(contention_comparison
        PRIVATE scheduler scheduler_warnings scheduler_sanitizers
    )
endif()
if(SCHEDULER_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    include(GoogleTest)
    function(scheduler_add_test name)
        add_executable(${name}
            tests/${name}.cpp
        )
        target_link_libraries(${name}
            PRIVATE
                scheduler
                GTest::gtest_main
                scheduler_warnings
                scheduler_sanitizers
        )
        gtest_discover_tests(${name})
    endfunction()
    scheduler_add_test(test_thread_pool)
    scheduler_add_test(test_priority)
    scheduler_add_test(test_scheduler)
    scheduler_add_test(test_concurrency)
endif()
if(SCHEDULER_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
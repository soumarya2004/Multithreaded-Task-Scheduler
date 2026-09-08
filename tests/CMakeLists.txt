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
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE scheduler GTest::gtest_main scheduler_warnings scheduler_sanitizers)
    gtest_discover_tests(${name})
endfunction()
scheduler_add_test(test_thread_pool)
scheduler_add_test(test_priority)
scheduler_add_test(test_scheduler)
scheduler_add_test(test_concurrency)

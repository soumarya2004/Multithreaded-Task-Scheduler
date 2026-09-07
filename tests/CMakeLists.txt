add_executable(priority_starvation_test
    priority_starvation_test.cpp
)
target_link_libraries(priority_starvation_test
    PRIVATE scheduler
)
add_test(
    NAME priority_starvation_test
    COMMAND priority_starvation_test
)
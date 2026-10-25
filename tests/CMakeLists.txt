add_executable(dupman_tests
    unit/result_test.cpp
)

target_link_libraries(dupman_tests PRIVATE dupman_core)
target_include_directories(dupman_tests PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

add_test(NAME result_test COMMAND dupman_tests)

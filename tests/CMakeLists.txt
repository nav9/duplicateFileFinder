add_executable(dupli_tests
    unit/result_test.cpp
)

target_link_libraries(dupli_tests PRIVATE dupli_core)
target_include_directories(dupli_tests PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

add_test(NAME result_test COMMAND dupli_tests)

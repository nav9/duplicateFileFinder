# Each test file is its own executable so a crash in one area does not hide the
# results of the others (CTest reports per test).
function(dupli_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE dupli_core dupli_platform)
    target_include_directories(${name} PRIVATE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/tests/unit>
    )
    target_compile_features(${name} PRIVATE cxx_std_20)
    # Sources use assert(); without this a Release build would compile them to
    # nothing and every test would pass vacuously.
    if(MSVC)
        target_compile_options(${name} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${name} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

dupli_add_test(result_test unit/result_test.cpp)
dupli_add_test(scanner_test unit/scanner_test.cpp)
dupli_add_test(planner_test unit/planner_test.cpp)
dupli_add_test(execution_test unit/execution_test.cpp)
dupli_add_test(report_test unit/report_test.cpp)
dupli_add_test(text_dedup_test unit/text_dedup_test.cpp)
dupli_add_test(useless_files_test unit/useless_files_test.cpp)
dupli_add_test(image_resize_test unit/image_resize_test.cpp)
dupli_add_test(integration_test integration/service_integration_test.cpp)

# The CLI must be safe with no arguments: it prints usage and touches nothing.
add_test(NAME cli_usage COMMAND dupli_cli)

set_tests_properties(result_test scanner_test planner_test execution_test report_test
    text_dedup_test useless_files_test image_resize_test
    PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

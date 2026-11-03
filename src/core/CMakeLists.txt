add_library(dupli_core STATIC
    result.cpp
    json.cpp
    scanner.cpp
    grouping.cpp
    action_planner.cpp
    validator.cpp
    executor.cpp
    journal.cpp
    report.cpp
    service.cpp
    text_dedup.cpp
    useless_files.cpp
    image_resize.cpp
)

target_include_directories(dupli_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# BLAKE3 headers are vendored next to the pinned sources; the library itself is
# provided by the third_party/blake3 target added by the top-level CMakeLists.
target_include_directories(dupli_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/blake3>
)

target_compile_features(dupli_core PUBLIC cxx_std_20)
target_link_libraries(dupli_core PUBLIC blake3)
dupli_enable_warnings(dupli_core)

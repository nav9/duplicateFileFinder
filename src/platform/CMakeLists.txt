add_library(dupli_platform STATIC
    filesystem.cpp
)

target_link_libraries(dupli_platform PUBLIC dupli_core)
target_include_directories(dupli_platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(dupli_platform PUBLIC cxx_std_20)

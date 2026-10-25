add_library(dupman_platform STATIC
    filesystem.cpp
)

target_link_libraries(dupman_platform PUBLIC dupman_core)
target_include_directories(dupman_platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(dupman_platform PUBLIC cxx_std_20)

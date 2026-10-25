add_library(dupman_core STATIC
    result.cpp
)

target_include_directories(dupman_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(dupman_core PUBLIC cxx_std_20)

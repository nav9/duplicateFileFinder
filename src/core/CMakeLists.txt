add_library(dupli_core STATIC
    result.cpp
)

target_include_directories(dupli_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(dupli_core PUBLIC cxx_std_20)
target_link_libraries(dupli_core PUBLIC blake3)

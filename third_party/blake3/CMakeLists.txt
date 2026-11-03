# Minimal CMake wrapper for the vendored BLAKE3 1.5.0 C implementation.
#
# The upstream CMakeLists.txt is not used because it configures a pkg-config file
# into ${CMAKE_BINARY_DIR} and installs it from there; as a subproject that path
# points at the top-level build directory and `cmake --install` fails. The sources
# are pinned and unmodified; only the build wrapper is ours.
#
# The portable C implementation is used on every platform so no assembly
# toolchain is required. BLAKE3's dispatch layer is told to skip SIMD; adding the
# upstream SIMD sources/flags later would only change speed, not results.

add_library(blake3 STATIC
    blake3.c
    blake3_dispatch.c
    blake3_portable.c
)

target_include_directories(blake3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(blake3 PRIVATE c_std_99)

# Portable build: no SSE/AVX/NEON dispatch. This keeps builds reproducible on
# x86, ARM and any other target without architecture-specific assembly.
target_compile_definitions(blake3 PRIVATE
    BLAKE3_NO_SSE2
    BLAKE3_NO_SSE41
    BLAKE3_NO_AVX2
    BLAKE3_NO_AVX512
    BLAKE3_USE_NEON=0
)

add_library(BLAKE3::blake3 ALIAS blake3)

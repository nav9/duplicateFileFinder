cmake_minimum_required(VERSION 3.16)
project(DuplicateManager VERSION 0.1.0 LANGUAGES C CXX)

# The core/CLI/tests must build without wxWidgets; the GUI is an optional
# add-on that is only configured when wxWidgets is actually discoverable.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

enable_testing()

# Warnings are applied per project target (never globally) so that the vendored
# BLAKE3 C/assembly sources are not forced through our C++ warning policy.
function(dupli_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

# BLAKE3 is vendored in-tree (pinned 1.5.0) so builds are reproducible and work
# offline. It must be added before any target links against it.
add_subdirectory(third_party/blake3)

add_subdirectory(src)
add_subdirectory(tests)

# GUI is optional and isolated: nothing outside src/gui may depend on wxWidgets.
find_package(wxWidgets QUIET COMPONENTS core base)
if(wxWidgets_FOUND)
    include(${wxWidgets_USE_FILE})
    add_subdirectory(src/gui)
    message(STATUS "wxWidgets found: GUI target dupli_gui will be built")
else()
    message(STATUS "wxWidgets not found: GUI target dupli_gui is skipped")
endif()

# --- Install rules (used by `menu.py` option 6) -----------------------------
include(GNUInstallDirs)
install(TARGETS dupli_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(wxWidgets_FOUND)
    install(TARGETS dupli_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# CPack produces an OS-appropriate archive without requiring elevation.
set(CPACK_PACKAGE_NAME "dupli")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform duplicate and unique file manager")
set(CPACK_PACKAGE_CONTACT "dupli maintainers <dupli@localhost>")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "dupli maintainers <dupli@localhost>")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
endif()
include(CPack)

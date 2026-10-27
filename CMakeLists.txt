cmake_minimum_required(VERSION 3.16)
project(DuplicateManager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()

# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
find_package(Blake3 REQUIRED)

# GUI target (optional, requires wxWidgets)
find_package(wxWidgets QUIET COMPONENTS core base)
if(wxWidgets_FOUND)
    include(${wxWidgets_USE_FILE})
    add_subdirectory(src/gui)
    message(STATUS "wxWidgets found, GUI will be built")
else()
    message(STATUS "wxWidgets not found, GUI will not be built")
endif()

# Add GUI target even if wxWidgets not found (will fail to build but files exist)
if(NOT wxWidgets_FOUND)
    message(WARNING "wxWidgets not found - GUI target will not be built. Install libwxgtk3.0-gtk3-dev to build GUI.")
endif()

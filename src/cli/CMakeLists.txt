add_executable(dupman_cli
    main.cpp
)

target_link_libraries(dupman_cli PRIVATE dupman_core dupman_platform)
target_include_directories(dupman_cli PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

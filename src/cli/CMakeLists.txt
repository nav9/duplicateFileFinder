add_executable(dupli_cli
    main.cpp
)

target_link_libraries(dupli_cli PRIVATE dupli_core dupli_platform)
target_include_directories(dupli_cli PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

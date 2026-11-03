# GUI target: configured only when the top-level CMakeLists found wxWidgets.
#
# Every translation unit used by the window is listed explicitly. The earlier
# draft widgets (duplicate_files_tab / scan_thread / taskhistory_panel) were
# replaced by scan_tab / operation_thread / history_tab and are no longer built.
add_executable(dupli_gui
    main.cpp
    mainframe.cpp
    async_task.cpp
    platform_paths.cpp
    settings.cpp
    logger.cpp
    taskmanager.cpp
    file_dialog_helper.cpp
    operation_thread.cpp
    plan_dialog.cpp
    result_view.cpp
    scan_tab.cpp
    history_tab.cpp
    settings_tab.cpp
    unique_sentences_tab.cpp
    useless_files_tab.cpp
    resize_images_tab.cpp
    image_codec_wx.cpp
)

target_include_directories(dupli_gui PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

target_link_libraries(dupli_gui PRIVATE
    dupli_core
    dupli_platform
    ${wxWidgets_LIBRARIES}
)

target_compile_features(dupli_gui PRIVATE cxx_std_20)
if(wxWidgets_DEFINITIONS)
    target_compile_definitions(dupli_gui PRIVATE ${wxWidgets_DEFINITIONS})
endif()

# The GUI is optional and cannot be built on machines without wxWidgets, so it is
# intentionally not part of the warnings-as-errors policy that guards the core.
if(MSVC)
    target_compile_options(dupli_gui PRIVATE /W3)
else()
    # wxWidgets' sizer flags are enum values that must be OR-ed together; GCC 13+
    # warns about that idiom, so the warning is disabled for the GUI only. It is
    # not a defect in this project's code and the core remains warnings-as-errors.
    target_compile_options(dupli_gui PRIVATE -Wall -Wextra
        -Wno-deprecated-enum-enum-conversion)
endif()

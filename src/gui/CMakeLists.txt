if(wxWidgets_FOUND)
    add_executable(dupli_gui
        main.cpp
        mainframe.cpp
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
    target_compile_definitions(dupli_gui PRIVATE ${wxWidgets_DEFINITIONS})
endif()
    target_sources(dupli_gui PRIVATE
        taskmanager.cpp
    )
    target_sources(dupli_gui PRIVATE
        taskmanager.cpp
        platform_paths.cpp
    )
    target_sources(dupli_gui PRIVATE
        taskmanager.cpp
        platform_paths.cpp
        taskhistory_panel.cpp
    )
    target_compile_options(dupli_gui PRIVATE -w)
    target_sources(dupli_gui PRIVATE
        taskmanager.cpp
        platform_paths.cpp
        taskhistory_panel.cpp
        settings.cpp
    )
    target_sources(dupli_gui PRIVATE
        taskmanager.cpp
        platform_paths.cpp
        taskhistory_panel.cpp
        settings.cpp
        logger.cpp
    )
    target_sources(dupli_gui PRIVATE
        taskmanager.cpp
        platform_paths.cpp
        taskhistory_panel.cpp
        settings.cpp
        logger.cpp
        duplicate_files_tab.cpp
    )
    target_sources(dupli_gui PRIVATE
        taskmanager.cpp
        platform_paths.cpp
        taskhistory_panel.cpp
        settings.cpp
        logger.cpp
        duplicate_files_tab.cpp
        file_dialog_helper.cpp
    )
    target_sources(dupli_gui PRIVATE
        taskmanager.cpp
        platform_paths.cpp
        taskhistory_panel.cpp
        settings.cpp
        logger.cpp
        duplicate_files_tab.cpp
        file_dialog_helper.cpp
        scan_thread.cpp
    )

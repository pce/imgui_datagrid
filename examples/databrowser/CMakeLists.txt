set(APP_NAME databrowser)


set(APP_SOURCES
    main.cpp
    app.cpp
    data_browser.cpp
    theme.cpp
    utils.cpp
    inspector/schema_inspector.cpp
    sokol_impl.cpp
    image_cache.cpp
    stb_impl.cpp
    ui/drag_drop_dialog.cpp
    ui/hex_view.cpp
    ui/sql_editor.cpp
    ui/text_area.cpp
    # Headers listed for IDE visibility:
    ui/drag_drop_dialog.hpp
    ui/hex_view.hpp
    ui/sql_editor.hpp
    ui/text_area.hpp
    ui/filetype_sniffer.hpp
)

add_executable(${APP_NAME} ${APP_SOURCES})


target_compile_features(${APP_NAME} PRIVATE cxx_std_23)


target_include_directories(${APP_NAME}
    PRIVATE
        ${SOKOL_INCLUDE_DIR}
        ${imgui_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${STB_INCLUDE_DIR}
)


# sokol_impl.cpp contains the SOKOL_*_IMPL defines and must be compiled with the
# correct backend define.  On Apple it also needs Objective-C++ (Cocoa/MetalKit).
if (APPLE)
    target_compile_definitions(${APP_NAME} PRIVATE SOKOL_METAL)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/sokol_impl.cpp
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )
    target_link_libraries(${APP_NAME} PRIVATE
        "-framework Metal"
        "-framework MetalKit"
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework AudioToolbox"
    )

elseif (WIN32)
    target_compile_definitions(${APP_NAME} PRIVATE SOKOL_D3D11)
    target_link_libraries(${APP_NAME} PRIVATE d3d11 dxgi)

elseif (CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_compile_definitions(${APP_NAME} PRIVATE SOKOL_WGPU)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    target_link_options(${APP_NAME} PRIVATE
        "-s USE_WEBGPU=1"
        "-s ASYNCIFY"
        "-s WASM=1"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/resources@/resources"
    )

else()
    target_compile_definitions(${APP_NAME} PRIVATE SOKOL_GLCORE)
    find_package(OpenGL REQUIRED)
    find_package(X11 QUIET)
    target_link_libraries(${APP_NAME} PRIVATE OpenGL::GL dl pthread)
    if (X11_FOUND)
        target_link_libraries(${APP_NAME} PRIVATE X11::X11)
    endif()
endif()


target_link_libraries(${APP_NAME}
    PRIVATE
        imgui_datagrid_lib
        $<LINK_LIBRARY:WHOLE_ARCHIVE,datagrid_adapters>
        imgui
        nlohmann_json::nlohmann_json
)

if (TARGET datagrid_asan)
    target_link_libraries(${APP_NAME} PRIVATE datagrid_asan)
endif()

if (TARGET datagrid_ubsan)
    target_link_libraries(${APP_NAME} PRIVATE datagrid_ubsan)
endif()


add_custom_command(
    TARGET ${APP_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:${APP_NAME}>/resources
    COMMENT "Copying resources → build directory"
)

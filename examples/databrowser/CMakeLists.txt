set(APP_NAME databrowser)

# ── Sources ───────────────────────────────────────────────────────────────────
set(APP_SOURCES
    main.cpp
    app.cpp
    data_browser.cpp
    theme.cpp
    utils.cpp
    inspector/schema_inspector.cpp
    sokol_impl.cpp
)

add_executable(${APP_NAME} ${APP_SOURCES})

# ── C++23 ─────────────────────────────────────────────────────────────────────
target_compile_features(${APP_NAME} PRIVATE cxx_std_23)

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(${APP_NAME}
    PRIVATE
        ${SOKOL_INCLUDE_DIR}           # sokol_app.h, sokol_gfx.h, util/sokol_imgui.h
        ${imgui_SOURCE_DIR}            # imgui.h  (needed by sokol_imgui.h)
        ${PROJECT_SOURCE_DIR}          # project root  (adapters/, imgui_datagrid.hpp)
        ${CMAKE_CURRENT_SOURCE_DIR}    # app/, data_browser.hpp, …
)

# ── Platform backend selection ────────────────────────────────────────────────
#
#  sokol_impl.cpp contains all SOKOL_*_IMPL defines and must be compiled with
#  the correct backend define.  On Apple it also needs Objective-C++ because
#  sokol_app.h uses Cocoa / MetalKit APIs.
#
if (APPLE)
    # Metal backend — sokol_app.h requires Objective-C++ on Apple.
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
    # D3D11 backend — no extra system libs needed (always available on Windows).
    target_compile_definitions(${APP_NAME} PRIVATE SOKOL_D3D11)
    target_link_libraries(${APP_NAME} PRIVATE d3d11 dxgi)

elseif (CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # WebGPU backend via Emscripten.
    target_compile_definitions(${APP_NAME} PRIVATE SOKOL_WGPU)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    target_link_options(${APP_NAME} PRIVATE
        "-s USE_WEBGPU=1"
        "-s ASYNCIFY"
        "-s WASM=1"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/resources@/resources"
    )

else()
    # Linux / other POSIX — OpenGL core profile.
    target_compile_definitions(${APP_NAME} PRIVATE SOKOL_GLCORE)
    find_package(OpenGL REQUIRED)
    find_package(X11 QUIET)
    target_link_libraries(${APP_NAME} PRIVATE OpenGL::GL dl pthread)
    if (X11_FOUND)
        target_link_libraries(${APP_NAME} PRIVATE X11::X11)
    endif()
endif()

# ── Library dependencies ──────────────────────────────────────────────────────
target_link_libraries(${APP_NAME}
    PRIVATE
        imgui_datagrid_lib
        $<LINK_LIBRARY:WHOLE_ARCHIVE,datagrid_adapters>
        imgui
        nlohmann_json::nlohmann_json
)

# ── Resources ─────────────────────────────────────────────────────────────────
add_custom_command(
    TARGET ${APP_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources
        $<TARGET_FILE_DIR:${APP_NAME}>/resources
    COMMENT "Copying resources → build directory"
)

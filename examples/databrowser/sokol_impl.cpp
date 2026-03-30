/// sokol_impl.cpp — single translation unit for all Sokol single-header
/// implementations.
///
/// BACKEND SELECTION
///   The active GPU backend is chosen at CMake configure time via a
///   compile-definition (SOKOL_METAL / SOKOL_D3D11 / SOKOL_GLCORE /
///   SOKOL_WGPU).  Do NOT define it here — let CMake own it so that
///   platform detection is centralised in the build system.
///
/// OBJECTIVE-C++ ON APPLE
///   When targeting macOS / iOS, CMake sets the compile flag
///   `-x objective-c++` on this file.  That triggers the Cocoa / MetalKit
///   integration inside sokol_app.h transparently — no .mm extension
///   or manual ObjC syntax needed in this file.
///
/// INCLUDE ORDER
///   sokol_gfx.h   must precede sokol_glue.h  (glue calls gfx functions)
///   sokol_app.h   must precede sokol_glue.h  (glue calls sapp functions)
///   sokol_imgui.h must follow sokol_gfx.h    (uses sg_* draw commands)
///
/// LICENSE: MIT (Sokol by Andre Weissflog / floooh)

// ── sokol_gfx ─────────────────────────────────────────────────────────────────
#define SOKOL_GFX_IMPL
#include "sokol_gfx.h"

// ── sokol_app ─────────────────────────────────────────────────────────────────
#define SOKOL_APP_IMPL
#include "sokol_app.h"

// ── sokol_glue ────────────────────────────────────────────────────────────────
// Bridges sokol_app's swapchain descriptor into sokol_gfx's environment /
// swapchain structs.  Requires both sokol_gfx.h and sokol_app.h above.
#define SOKOL_GLUE_IMPL
#include "sokol_glue.h"

// ── sokol_log ─────────────────────────────────────────────────────────────────
// Provides slog_func — the default logger passed to sg_desc / sapp_desc.
#define SOKOL_LOG_IMPL
#include "sokol_log.h"

// ── sokol_imgui ───────────────────────────────────────────────────────────────
// Dear ImGui integration over sokol_gfx.  Replaces imgui_impl_opengl3 /
// imgui_impl_glfw entirely.
//
// sokol_imgui.h requires imgui.h to be included BEFORE the implementation
// block.  The header is reachable because the databrowser target adds
// ${imgui_SOURCE_DIR} to its include path (see CMakeLists.txt).
#include "imgui.h"
#define SOKOL_IMGUI_IMPL
#include "util/sokol_imgui.h"

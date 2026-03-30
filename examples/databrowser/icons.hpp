#pragma once
// ============================================================
//  icons.hpp — FontAwesome 6 Free (Solid) icon constants
//
//  SETUP
//  ─────
//  1. Download FontAwesome 6 Free from https://fontawesome.com/download
//  2. Copy  fa-solid-900.ttf  to  resources/fonts/
//
//  If the font file is missing, App::Init() falls back gracefully to
//  the main Roboto font — icons will render as replacement glyphs (□).
//
//  All codepoints are FontAwesome 5/6 Solid, private-use range U+F000–U+F8FF.
//  UTF-8 encoded inline so no extra headers are needed.
//
//  LICENSE: font — SIL OFL 1.1 · code — MIT
// ============================================================

#include "imgui.h"

namespace Icons {

// Atlas merge range — pass kFAGlyphRanges to ImFontConfig::GlyphRanges
inline constexpr ImWchar kFAGlyphRanges[] = { 0xE000u, 0xF8FFu, 0u };

// ── Navigation ────────────────────────────────────────────────────────────
inline constexpr const char* ArrowUp      = "\xef\x81\xa2";  // U+F062  arrow-up
inline constexpr const char* Home         = "\xef\x80\x95";  // U+F015  house
inline constexpr const char* ChevronRight = "\xef\x81\x94";  // U+F054  chevron-right
inline constexpr const char* ChevronLeft  = "\xef\x81\x93";  // U+F053  chevron-left

// ── CRUD / toolbar ────────────────────────────────────────────────────────
inline constexpr const char* Pencil       = "\xef\x81\x84";  // U+F044  pen
inline constexpr const char* Plus         = "\xef\x81\xa7";  // U+F067  plus
inline constexpr const char* Minus        = "\xef\x81\xa8";  // U+F068  minus
inline constexpr const char* Trash        = "\xef\x87\xb8";  // U+F1F8  trash
inline constexpr const char* Save         = "\xef\x83\x87";  // U+F0C7  floppy-disk
inline constexpr const char* Refresh      = "\xef\x80\xa1";  // U+F021  arrows-rotate
inline constexpr const char* Search       = "\xef\x80\x82";  // U+F002  magnifying-glass
inline constexpr const char* Filter       = "\xef\x82\xb0";  // U+F0B0  filter
inline constexpr const char* Eye          = "\xef\x81\xae";  // U+F06E  eye
inline constexpr const char* EyeSlash     = "\xef\x81\xb0";  // U+F070  eye-slash

// ── Status / indicators ───────────────────────────────────────────────────
inline constexpr const char* InfoCircle   = "\xef\x81\x9a";  // U+F05A  circle-info
inline constexpr const char* Check        = "\xef\x80\x8c";  // U+F00C  check
inline constexpr const char* Times        = "\xef\x80\x8d";  // U+F00D  xmark
inline constexpr const char* Warning      = "\xef\x81\xb1";  // U+F071  triangle-exclamation
inline constexpr const char* Lock         = "\xef\x80\xa3";  // U+F023  lock

// ── Filesystem ────────────────────────────────────────────────────────────
inline constexpr const char* Folder       = "\xef\x81\xbb";  // U+F07B  folder
inline constexpr const char* FolderOpen   = "\xef\x81\xbc";  // U+F07C  folder-open
inline constexpr const char* File         = "\xef\x80\x96";  // U+F016  file
inline constexpr const char* FileImage    = "\xef\x87\x85";  // U+F1C5  file-image
inline constexpr const char* Image        = "\xef\x80\xbe";  // U+F03E  image

// ── Data / adapters ───────────────────────────────────────────────────────
inline constexpr const char* Database     = "\xef\x87\x80";  // U+F1C0  database
inline constexpr const char* TableIcon    = "\xef\x80\x8e";  // U+F00E  table-cells

// ── Layout / UI ───────────────────────────────────────────────────────────
inline constexpr const char* Bars          = "\xef\x83\x89";  // U+F0C9  bars (hamburger)
inline constexpr const char* SidebarToggle = "\xef\x83\x89";  // U+F0C9  (alias)
inline constexpr const char* Code          = "\xef\x84\xa1";  // U+F121  code

} // namespace Icons

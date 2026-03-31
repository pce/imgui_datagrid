#!/usr/bin/env python3
"""Patch DuckDB v1.5.1 amalgamation for LLVM 22 / C++23 compatibility.

QueryMetrics has  unique_ptr<ActiveTimer> latency_timer  while ActiveTimer is
only forward-declared at the point where QueryMetrics' class body is parsed.
With LLVM 22 / libc++ C++23, unique_ptr<T>'s constexpr destructor (and its
constexpr default constructor) require T to be complete when the containing
class's constructor or destructor is defined inline.

Fix applied to duckdb.hpp:
  1. Replace the inline in-class constructor body  QueryMetrics() { Reset(); }
     with a bare declaration  QueryMetrics();
  2. Remove the inline out-of-class destructor definition (if present).
  3. Inject an explicit in-class ~QueryMetrics(); declaration.

Fix applied to duckdb.cpp (right after #include "duckdb.hpp", where ActiveTimer
is fully visible because the whole header has been processed):
  4. Add  inline QueryMetrics::QueryMetrics() { Reset(); }
  5. Add  QueryMetrics::~QueryMetrics() = default;

Both definitions are wrapped in  namespace duckdb { … }.

Usage (invoked by CMakeLists.txt):
    python3 cmake/patch_duckdb.py  <path/to/duckdb.hpp>
"""

import os
import re
import sys

# ── sentinels (presence of any → already patched) ────────────────────────────
PATCH_SENTINEL = "patch_duckdb.py:"

# ── hpp patterns ─────────────────────────────────────────────────────────────

# Inline ctor body in the class — matches the 3-line block:
#     QueryMetrics() {
#         Reset();
#     }
INLINE_CTOR_RE = re.compile(
    r"([ \t]*)QueryMetrics\(\)\s*\{\s*\n\s*Reset\(\);\s*\n\s*\}",
    re.MULTILINE,
)
CTOR_DECL_REPLACEMENT = r"\1QueryMetrics(); // patch_duckdb.py: decl only (LLVM22/C++23)"

# Inline out-of-class dtor (may or may not exist depending on DuckDB revision)
INLINE_DTOR = "inline QueryMetrics::~QueryMetrics() = default;"

# Inject explicit ~QueryMetrics(); after latency_timer member (tab-indented)
LATENCY_TIMER_LINE = "\tunique_ptr<ActiveTimer> latency_timer;"
DTOR_DECL_INJECTION = (
    "\n"
    "\t// patch_duckdb.py: explicit ~QueryMetrics() decl (LLVM22/C++23)\n"
    "\t~QueryMetrics();"
)

# ── cpp additions ─────────────────────────────────────────────────────────────
HPP_INCLUDE = '#include "duckdb.hpp"'
CPP_ADDITIONS = (
    "\n"
    "// patch_duckdb.py: QueryMetrics ctor+dtor defined after ActiveTimer is complete\n"
    "namespace duckdb {\n"
    "  inline QueryMetrics::QueryMetrics() { Reset(); }\n"
    "  QueryMetrics::~QueryMetrics() = default;\n"
    "}\n"
)


# ── helpers ───────────────────────────────────────────────────────────────────

def patch_hpp(path: str) -> bool:
    with open(path, encoding="utf-8") as fh:
        text = fh.read()

    if PATCH_SENTINEL in text:
        return False  # idempotent

    changed = False

    # 1. Replace inline in-class ctor body with a bare declaration
    new_text, n = INLINE_CTOR_RE.subn(CTOR_DECL_REPLACEMENT, text, count=1)
    if n:
        text = new_text
        changed = True

    # 2. Remove inline out-of-class dtor definition if present
    if INLINE_DTOR in text:
        text = text.replace(
            INLINE_DTOR,
            "// patch_duckdb.py: ~QueryMetrics() moved to duckdb.cpp (LLVM22/C++23)",
            1,
        )
        changed = True

    # 3. Inject explicit in-class ~QueryMetrics(); (needed so the out-of-class
    #    = default definition in duckdb.cpp is valid C++).
    if LATENCY_TIMER_LINE in text and "~QueryMetrics();" not in text:
        text = text.replace(
            LATENCY_TIMER_LINE,
            LATENCY_TIMER_LINE + DTOR_DECL_INJECTION,
            1,
        )
        changed = True

    if changed:
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
    return changed


def patch_cpp(path: str) -> bool:
    with open(path, encoding="utf-8") as fh:
        text = fh.read()

    if PATCH_SENTINEL in text:
        return False  # idempotent

    if HPP_INCLUDE not in text:
        print(
            f"[patch_duckdb] ERROR: '{HPP_INCLUDE}' not found in {path}",
            file=sys.stderr,
        )
        sys.exit(1)

    patched = text.replace(HPP_INCLUDE, HPP_INCLUDE + CPP_ADDITIONS, 1)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(patched)
    return True


# ── main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <path/to/duckdb.hpp>", file=sys.stderr)
        sys.exit(1)

    hpp_path = sys.argv[1]
    cpp_path = os.path.splitext(hpp_path)[0] + ".cpp"

    for p in (hpp_path, cpp_path):
        if not os.path.isfile(p):
            print(f"[patch_duckdb] ERROR: file not found: {p}", file=sys.stderr)
            sys.exit(1)

    hpp_changed = patch_hpp(hpp_path)
    cpp_changed = patch_cpp(cpp_path)

    name = os.path.basename(hpp_path)
    if hpp_changed or cpp_changed:
        print(
            f"[patch_duckdb] applied LLVM 22 / C++23 fix to "
            f"{name} + {os.path.splitext(name)[0]}.cpp"
        )
    else:
        print("[patch_duckdb] already patched — nothing to do")


if __name__ == "__main__":
    main()


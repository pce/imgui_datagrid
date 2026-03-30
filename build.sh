#!/usr/bin/env bash
# =============================================================================
# build.sh -- DX build script for imgui_datagrid
# =============================================================================
#
# Usage: ./build.sh [options]
#
#   -c, --clean     Wipe the build directory before configuring
#                   (default: incremental -- only re-runs CMake if needed)
#   -r, --release   Build in Release mode          (default: Debug)
#   -t, --test      Build & run Catch2 tests after a successful build
#   -x, --run       Launch the app after a successful build
#   -v, --verbose   Enable verbose CMake / compiler output
#   -j N            Parallel jobs                  (default: logical CPU count)
#   -h, --help      Show this help message
#
# Examples:
#   ./build.sh                    # fast incremental debug build
#   ./build.sh -c                 # clean + debug build
#   ./build.sh -r -x              # release build, then launch app
#   ./build.sh -t                 # debug build + run tests
#   ./build.sh -c -r -t -j 8     # clean release build with tests, 8 jobs
# =============================================================================

set -euo pipefail

# -- Colours (only when stdout is an interactive terminal) --------------------
if [ -t 1 ]; then
    C_RED='\033[0;31m'
    C_GREEN='\033[0;32m'
    C_YELLOW='\033[1;33m'
    C_CYAN='\033[0;36m'
    C_BOLD='\033[1m'
    C_DIM='\033[2m'
    C_RESET='\033[0m'
else
    C_RED=''
    C_GREEN=''
    C_YELLOW=''
    C_CYAN=''
    C_BOLD=''
    C_DIM=''
    C_RESET=''
fi

info()    { printf "${C_CYAN}>>>${C_RESET} %s\n"       "$*"; }
success() { printf "${C_GREEN}OK ${C_RESET} %s\n"      "$*"; }
warn()    { printf "${C_YELLOW}!  ${C_RESET} %s\n"     "$*"; }
step()    { printf "\n${C_BOLD}--- %s${C_RESET} %s\n"  "$1" "${2:-}"; }
die()     { printf "${C_RED}ERR${C_RESET} %s\n"        "$*" >&2; exit 1; }

# -- Resolve the directory that contains this script -------------------------
# Works correctly when called as ./build.sh, bash build.sh, or via a symlink.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# -- Defaults -----------------------------------------------------------------
CLEAN=0
BUILD_TYPE="Debug"
RUN_TESTS=0
RUN_APP=0
VERBOSE=0
BUILD_DIR="${SCRIPT_DIR}/build"

# APP_NAME must match the project() name used in examples/databrowser/CMakeLists.txt
APP_NAME="databrowser"

# Auto-detect logical CPU count (macOS: sysctl, Linux: nproc, fallback: 4)
if command -v sysctl >/dev/null 2>&1; then
    JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"
elif command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
else
    JOBS=4
fi

# -- Usage --------------------------------------------------------------------
usage() {
    cat <<HELP

${C_BOLD}Usage:${C_RESET} ./build.sh [options]

${C_BOLD}Options:${C_RESET}
  -c, --clean     Wipe build dir before configuring  (incremental by default)
  -r, --release   Release build                       (default: Debug)
  -t, --test      Build and run Catch2 tests
  -x, --run       Launch the app after a successful build
  -v, --verbose   Verbose CMake / compiler output
  -j N            Parallel jobs                       (default: ${JOBS})
  -h, --help      Show this help message

${C_BOLD}Examples:${C_RESET}
  ${C_DIM}./build.sh${C_RESET}                   incremental debug build
  ${C_DIM}./build.sh -c -r${C_RESET}             clean release build
  ${C_DIM}./build.sh -t -x${C_RESET}             build, test, then launch
  ${C_DIM}./build.sh -c -r -t -j 8${C_RESET}     clean release, 8 jobs, run tests

HELP
}

# -- Parse arguments ----------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
        -c|--clean)   CLEAN=1 ;;
        -r|--release) BUILD_TYPE="Release" ;;
        -t|--test)    RUN_TESTS=1 ;;
        -x|--run)     RUN_APP=1 ;;
        -v|--verbose) VERBOSE=1 ;;
        -j)
            shift
            [ -z "${1:-}" ] && die "-j requires a numeric argument"
            JOBS="$1"
            ;;
        -h|--help)    usage; exit 0 ;;
        *)            warn "Unknown option: $1"; usage; exit 1 ;;
    esac
    shift
done

# -- Banner -------------------------------------------------------------------
printf "\n${C_BOLD}imgui_datagrid${C_RESET}"
printf "  build=${C_BOLD}%s${C_RESET}" "$BUILD_TYPE"
printf "  jobs=${C_BOLD}%s${C_RESET}"  "$JOBS"
[ "$CLEAN"     = "1" ] && printf "  ${C_YELLOW}[clean]${C_RESET}"
[ "$RUN_TESTS" = "1" ] && printf "  ${C_CYAN}[tests]${C_RESET}"
[ "$RUN_APP"   = "1" ] && printf "  ${C_GREEN}[run]${C_RESET}"
printf "\n"

# -- Clean --------------------------------------------------------------------
if [ "$CLEAN" = "1" ] && [ -d "$BUILD_DIR" ]; then
    step "Clean" "$BUILD_DIR"
    rm -rf "$BUILD_DIR"
    success "Build directory removed"
fi

mkdir -p "$BUILD_DIR"

# -- Configure ----------------------------------------------------------------
step "Configure" "[$BUILD_TYPE]"

if [ "$VERBOSE" = "1" ]; then
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
          --log-level=VERBOSE
else
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi

# Symlink compile_commands.json to repo root so clangd / any LSP picks it up
# without needing a .clangd config file pointing at the build directory.
if [ -f "$BUILD_DIR/compile_commands.json" ]; then
    ln -sf "$BUILD_DIR/compile_commands.json" \
           "$SCRIPT_DIR/compile_commands.json"
    info "compile_commands.json -> build/  (LSP / clangd ready)"
fi

# -- Build --------------------------------------------------------------------
step "Build"

T0="$SECONDS"

if [ "$VERBOSE" = "1" ]; then
    cmake --build "$BUILD_DIR" --parallel "$JOBS" --verbose
else
    cmake --build "$BUILD_DIR" --parallel "$JOBS"
fi

ELAPSED=$(( SECONDS - T0 ))
success "Build finished in ${ELAPSED}s"

# -- Tests --------------------------------------------------------------------
if [ "$RUN_TESTS" = "1" ]; then
    step "Tests"

    # Build the test target explicitly (defined in tests/CMakeLists.txt).
    cmake --build "$BUILD_DIR" --parallel "$JOBS" --target datagrid_tests

    # --test-dir requires CMake >= 3.20; we require 3.24 so this is safe.
    ctest --test-dir "$BUILD_DIR" \
          --output-on-failure \
          --parallel "$JOBS"

    success "All tests passed"
fi

# -- Run ----------------------------------------------------------------------
if [ "$RUN_APP" = "1" ]; then
    step "Launch" "$APP_NAME"

    # CMake places the binary under the subdirectory that mirrors the source
    # tree: build/examples/databrowser/databrowser
    # resources/ is also copied there (POST_BUILD command in its CMakeLists),
    # so we must cd into that directory before launching.
    APP_OUT_DIR="${BUILD_DIR}/examples/databrowser"
    APP_BIN="${APP_OUT_DIR}/${APP_NAME}"
    [ -x "$APP_BIN" ] || die "Binary not found: ${APP_BIN}"

    info "Working directory: ${APP_OUT_DIR}"

    # Change into the binary's output dir so relative resource paths resolve.
    cd "$APP_OUT_DIR"

    # Do not let a non-zero app exit code fail the overall script.
    # Closing the window normally returns 0; a crash returns non-zero, and
    # we want a readable message rather than a silent pipefail exit.
    set +e
    ./"$APP_NAME"
    APP_EXIT=$?
    set -e

    if [ "$APP_EXIT" != "0" ]; then
        warn "${APP_NAME} exited with code ${APP_EXIT}"
    else
        success "${APP_NAME} exited cleanly"
    fi
fi

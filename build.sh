#!/usr/bin/env bash
set -euo pipefail

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

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

CLEAN=0
BUILD_TYPE="Debug"
USE_DUCKDB=1
RUN_TESTS=0
RUN_APP=0
VERBOSE=0
BUILD_DIR="${SCRIPT_DIR}/build"

USE_HOME_LLVM=0
LLVM_TOOLCHAIN_PATH=""
# Pick the highest-numbered llvm-* toolchain installed in ~/toolchains/.
_llvm_candidate=$(ls -d "${HOME}/toolchains/llvm-"* 2>/dev/null | sort -V | tail -1)
if [ -n "$_llvm_candidate" ] && [ -x "${_llvm_candidate}/bin/clang" ]; then
    LLVM_TOOLCHAIN_PATH="$_llvm_candidate"
    export LLVM_TOOLCHAIN_PATH
    USE_HOME_LLVM=1
fi

# APP_NAME must match the project() name in examples/databrowser/CMakeLists.txt
APP_NAME="databrowser"

# Auto-detect logical CPU count (macOS: sysctl, Linux: nproc, fallback: 4)
if command -v sysctl >/dev/null 2>&1; then
    JOBS="$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"
elif command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
else
    JOBS=4
fi

usage() {
    cat <<HELP

${C_BOLD}Usage:${C_RESET} ./build.sh [options]

${C_BOLD}Options:${C_RESET}
  -c, --clean       Wipe build dir before configuring  (incremental by default)
  -r, --release     Release build                       (default: Debug + ASan/UBSan)
  -d, --duckdb      Enable DuckDB adapter               (default: ON)
  --no-duckdb       Disable DuckDB adapter
  -t, --test        Build and run Catch2 tests
  -x, --run         Launch the app after a successful build
  -v, --verbose     Verbose CMake / compiler output
  -j N              Parallel jobs                       (default: ${JOBS})
  -h, --help        Show this help message

${C_BOLD}Examples:${C_RESET}
  ${C_DIM}./build.sh${C_RESET}                     incremental debug build (DuckDB on)
  ${C_DIM}./build.sh --no-duckdb${C_RESET}         build without DuckDB
  ${C_DIM}./build.sh -c -r${C_RESET}               clean release build
  ${C_DIM}./build.sh -r -x${C_RESET}               release build with DuckDB, then launch
  ${C_DIM}./build.sh -t -x${C_RESET}               build, test, then launch
  ${C_DIM}./build.sh -c -r -t -j 8${C_RESET}       clean release, 8 jobs, run tests

HELP
}

while [ $# -gt 0 ]; do
    case "$1" in
        -c|--clean)    CLEAN=1 ;;
        -r|--release)  BUILD_TYPE="Release" ;;
        -d|--duckdb)   USE_DUCKDB=1 ;;
        --no-duckdb)   USE_DUCKDB=0 ;;
        -t|--test)     RUN_TESTS=1 ;;
        -x|--run)      RUN_APP=1 ;;
        -v|--verbose)  VERBOSE=1 ;;
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

printf "\n${C_BOLD}imgui_datagrid${C_RESET}"
printf "  build=${C_BOLD}%s${C_RESET}" "$BUILD_TYPE"
printf "  jobs=${C_BOLD}%s${C_RESET}"  "$JOBS"
[ "$CLEAN"      = "1" ] && printf "  ${C_YELLOW}[clean]${C_RESET}"
[ "$USE_HOME_LLVM" = "1" ] && printf "  ${C_CYAN}[$(basename "$LLVM_TOOLCHAIN_PATH")]${C_RESET}"
[ "$BUILD_TYPE" != "Release" ] && printf "  ${C_CYAN}[asan+ubsan]${C_RESET}"
[ "$USE_DUCKDB" = "1" ] && printf "  ${C_CYAN}[duckdb]${C_RESET}"
[ "$RUN_TESTS" = "1" ] && printf "  ${C_CYAN}[tests]${C_RESET}"
[ "$RUN_APP"   = "1" ] && printf "  ${C_GREEN}[run]${C_RESET}"
printf "\n"

if [ "$CLEAN" = "1" ] && [ -d "$BUILD_DIR" ]; then
    step "Clean" "$BUILD_DIR"
    rm -rf "$BUILD_DIR"
    success "Build directory removed"
fi

mkdir -p "$BUILD_DIR"

step "Configure" "[$BUILD_TYPE]"

CMAKE_EXTRA_FLAGS=()
[ "$USE_DUCKDB" = "1" ] && CMAKE_EXTRA_FLAGS+=("-DDATAGRID_USE_DUCKDB=ON")
[ "$USE_DUCKDB" = "0" ] && CMAKE_EXTRA_FLAGS+=("-DDATAGRID_USE_DUCKDB=OFF")

if [ "$USE_HOME_LLVM" = "1" ]; then
    LLVM_BIN="${LLVM_TOOLCHAIN_PATH}/bin"
    # Use the LLVM compiler; let it pick its own default C++ standard library.
    # No -stdlib=libc++ or linker-flag overrides: plain C++23 via the clang frontend.
    CMAKE_EXTRA_FLAGS+=("-DCMAKE_C_COMPILER=${LLVM_BIN}/clang")
    CMAKE_EXTRA_FLAGS+=("-DCMAKE_CXX_COMPILER=${LLVM_BIN}/clang++")
    info "Using LLVM toolchain: ${LLVM_TOOLCHAIN_PATH}"
fi

if [ "$BUILD_TYPE" = "Release" ]; then
    CMAKE_EXTRA_FLAGS+=("-DDATAGRID_ENABLE_ASAN=OFF")
    CMAKE_EXTRA_FLAGS+=("-DDATAGRID_ENABLE_UBSAN=OFF")
else
    CMAKE_EXTRA_FLAGS+=("-DDATAGRID_ENABLE_ASAN=ON")
    CMAKE_EXTRA_FLAGS+=("-DDATAGRID_ENABLE_UBSAN=ON")
fi

if [ "$VERBOSE" = "1" ]; then
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
          ${CMAKE_EXTRA_FLAGS[@]+"${CMAKE_EXTRA_FLAGS[@]}"} \
          --log-level=VERBOSE
else
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
          ${CMAKE_EXTRA_FLAGS[@]+"${CMAKE_EXTRA_FLAGS[@]}"}
fi

# Symlink compile_commands.json to repo root so clangd / any LSP picks it up
# without a .clangd config pointing at the build directory.
if [ -f "$BUILD_DIR/compile_commands.json" ]; then
    ln -sf "$BUILD_DIR/compile_commands.json" \
           "$SCRIPT_DIR/compile_commands.json"
    info "compile_commands.json -> build/  (LSP / clangd ready)"
fi

step "Build"

T0="$SECONDS"

if [ "$VERBOSE" = "1" ]; then
    if [ "$RUN_TESTS" = "1" ]; then
        cmake --build "$BUILD_DIR" --parallel "$JOBS" --target "$APP_NAME" datagrid_tests datagrid_imgui_tests --verbose
    else
        cmake --build "$BUILD_DIR" --parallel "$JOBS" --target "$APP_NAME" --verbose
    fi
else
    if [ "$RUN_TESTS" = "1" ]; then
        cmake --build "$BUILD_DIR" --parallel "$JOBS" --target "$APP_NAME" datagrid_tests datagrid_imgui_tests
    else
        cmake --build "$BUILD_DIR" --parallel "$JOBS" --target "$APP_NAME"
    fi
fi

ELAPSED=$(( SECONDS - T0 ))
success "Build finished in ${ELAPSED}s"

if [ "$RUN_TESTS" = "1" ]; then
    step "Tests"
    ctest --test-dir "$BUILD_DIR" \
          --output-on-failure \
          --parallel "$JOBS"
    success "All tests passed"
fi

if [ "$RUN_APP" = "1" ]; then
    step "Launch" "$APP_NAME"

    # CMake places the binary under build/examples/databrowser/; resources/ is
    # copied there by a POST_BUILD command, so we cd there before launching.
    APP_OUT_DIR="${BUILD_DIR}/examples/databrowser"
    APP_BIN="${APP_OUT_DIR}/${APP_NAME}"
    [ -x "$APP_BIN" ] || die "Binary not found: ${APP_BIN}"

    info "Working directory: ${APP_OUT_DIR}"
    cd "$APP_OUT_DIR"

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

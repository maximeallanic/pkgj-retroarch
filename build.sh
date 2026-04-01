#!/usr/bin/env bash
# build.sh — convenience build script for pkgj fork
#
# Usage:
#   ./build.sh [target] [--clean]
#
# Targets:
#   host          Build host simulator   (output: ci/buildhost/pkgj_cli)
#   vita          Build PS Vita release   (output: ci/build/pkgj.vpk)       [default]
#   vita-release  Same as vita
#   vita-test     Build PS Vita test      (output: ci/buildtest/pkgj.vpk)
#                   Title ID : PKGJ00099
#                   App name : "PKGj+ TEST"
#                   (Safe to install alongside the release build on the same Vita)
#
# Options:
#   --clean   Remove the build directory before building (full rebuild)

set -euo pipefail

# ── Resolve project root ──
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TARGET="vita"
CLEAN=0

for arg in "$@"; do
    case "$arg" in
        host|vita|vita-release|vita-test) TARGET="$arg" ;;
        --clean) CLEAN=1 ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [host|vita|vita-release|vita-test] [--clean]"
            exit 1
            ;;
    esac
done

[[ "$TARGET" == "vita-release" ]] && TARGET="vita"

# ── Compilers ──
export CC="${CC:-gcc-12}"
export CXX="${CXX:-g++-12}"

# ── VitaSDK ──
export VITASDK="${VITASDK:-/root/vitasdk}"
export PATH="$VITASDK/bin:$PATH"

# ── Must run conan from inside ci/ so Poetry finds pyproject.toml ──
cd "$SCRIPT_DIR/ci"

# ── Helper: clean build dir ──
maybe_clean() {
    local dir="$1"
    if [[ $CLEAN -eq 1 && -d "$dir" ]]; then
        echo "==> Removing $dir"
        rm -rf "$dir"
    fi
    mkdir -p "$dir"
}

# ── Build ──
case "$TARGET" in

    # ------------------------------------------------------------------
    host)
        echo "==> Building host simulator"
        maybe_clean buildhost
        cd buildhost

        poetry run conan install ../.. \
            -s build_type=RelWithDebInfo \
            -s compiler=gcc \
            -s compiler.version=12 \
            -s compiler.libcxx=libstdc++11 \
            --build missing \
            --output-folder .

        cmake ../.. \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DBUILD_SIM=ON

        ninja pkgj_cli pkgj_sim

        echo ""
        echo "==> Done.  Binaries: ci/buildhost/pkgj_cli  ci/buildhost/pkgj_sim"
        ;;

    # ------------------------------------------------------------------
    vita)
        echo "==> Building Vita release  (PKGJ00001 / PKGj+)"
        maybe_clean build
        cd build

        poetry run conan install ../.. \
            -s build_type=RelWithDebInfo \
            --profile:host vita \
            --build missing \
            --output-folder .

        poetry run conan build ../.. \
            -s build_type=RelWithDebInfo \
            --profile:host vita \
            --output-folder .

        # Keep ELF with debug symbols alongside the stripped eboot
        [[ -f pkgj ]] && cp pkgj pkgj.elf

        echo ""
        echo "==> Done.  Package: ci/build/pkgj.vpk"
        ;;

    # ------------------------------------------------------------------
    vita-test)
        echo "==> Building Vita TEST  (PKGJ00099 / PKGj+ TEST)"
        maybe_clean buildtest
        cd buildtest

        # Step 1: conan install — resolves deps, generates conan_toolchain.cmake
        poetry run conan install ../.. \
            -s build_type=RelWithDebInfo \
            --profile:host vita \
            --build missing \
            --output-folder .

        # Source the generated cross-compile env (sets CC/CXX/AR/STRIP/etc)
        # Pre-initialize variables that the generated file appends to, so that
        # set -u (nounset) does not abort when they are not already in the
        # environment (LD_LIBRARY_PATH / DYLD_LIBRARY_PATH are often absent).
        export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
        export DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH:-}"
        # shellcheck disable=SC1091
        [[ -f conanbuildenv-relwithdebinfo-armv7.sh ]] && \
            source conanbuildenv-relwithdebinfo-armv7.sh

        # Step 2: cmake configure — override Title ID and App Name for the test build
        #         VITA_TITLEID and VITA_APP_NAME are CACHE variables in CMakeLists.txt
        #         so -D flags here take precedence over the defaults.
        cmake ../.. \
            -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DVITA_TITLEID="PKGJ00099" \
            -DVITA_APP_NAME="PKGj+ TEST"

        # Step 3: cmake build
        cmake --build .

        [[ -f pkgj ]] && cp pkgj pkgj.elf

        echo ""
        echo "==> Done.  Package: ci/buildtest/pkgj.vpk"
        echo "    Title ID PKGJ00099 — safe to install alongside the release build."
        ;;
esac

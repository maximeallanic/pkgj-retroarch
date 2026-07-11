#!/usr/bin/env bash
# Compile+run the pure-logic unit tests with plain g++ (no conan, no VitaSDK).
# Add each new tests/test_*.cpp and the src/*.cpp unit(s) it exercises here.
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-g++-12}"
"$CXX" -std=c++17 -I src -I tests -o /tmp/pkgj_tests \
    tests/test_main.cpp \
    tests/test_smoke.cpp \
    tests/test_systems.cpp \
    tests/test_jsonscan.cpp \
    tests/test_romcache.cpp \
    tests/test_config_url.cpp \
    src/systems.cpp \
    src/jsonscan.cpp \
    src/romcache.cpp \
    src/config_url.cpp
/tmp/pkgj_tests "$@"

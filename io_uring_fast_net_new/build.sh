#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

CMAKE_OPTIONS="-DCMAKE_BUILD_TYPE=Release"
MAKE_JOBS=32

build_project() {
    echo "===== Building the Project ====="

    mkdir -p "$BUILD_DIR"

    cd "$BUILD_DIR"

    echo "Running cmake with options: ${CMAKE_OPTIONS}"
    cmake .. ${CMAKE_OPTIONS}

    echo "Compiling the project with make -j${MAKE_JOBS}"
    make -j"${MAKE_JOBS}"

    echo "===== Build Completed ====="
    echo ""
}

build_project
echo "===== All Operations Completed ====="

#!/bin/bash

set -e

usage() {
    echo "Usage: $0 [client|server|client_s|server_s]"
    exit 1
}

if [ "$#" -ne 1 ]; then
    usage
fi

MODE=$1
if [[ "$MODE" != "client" && "$MODE" != "server" && "$MODE" != "server_s" && "$MODE" != "client_s" ]]; then
    echo "Error: Invalid mode specified."
    usage
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

CMAKE_OPTIONS="-DCMAKE_BUILD_TYPE=Release"
MAKE_JOBS=32
RUN_DURATION_SECONDS=21

EXECUTABLE="${BUILD_DIR}/${MODE}"

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

run_test() {
    local connections_per_thread=$1
    local thread_count=$2
    local server_addr="10.0.1.190"

    local thread_count="${thread_count}"
    local connections_per_thread="${connections_per_thread}"

    echo "------------------------------------------------------------"
    echo "Configuration:"
    echo "  MODE: ${MODE}"
    echo "  SERVER_ADDR=${server_addr}"
    echo "  THREAD_COUNT=${thread_count}"
    echo "  CONNECTIONS_PER_THREAD=${connections_per_thread}"
    echo "  RUN_DURATION_SECONDS=${RUN_DURATION_SECONDS}"
    echo "------------------------------------------------------------"
    if [[ "$MODE" == "client" || "$MODE" == "client_s" ]]; then
        echo "Waiting 2 seconds before running this configuration..."
        sleep 2
    else
        echo "Running configuration immediately..."
    fi

    sudo -E SERVER_ADDR="${server_addr}" \
           THREAD_COUNT="${thread_count}" \
           CONNECTIONS_PER_THREAD="${connections_per_thread}" \
           RUN_DURATION_SECONDS="${RUN_DURATION_SECONDS}" \
           ALLOC_PIN="1" \
           ENABLE_NAGLE="1" \
           INCREASE_SOCKET_BUFFERS="0" \
           PIN_THREADS="1" \
           "${EXECUTABLE}"

    echo "Completed run with CONNECTIONS_PER_THREAD=${connections_per_thread}, THREAD_COUNT=${thread_count}"
    echo ""
}

perform_tests() {
    echo "===== Starting Tests for ${MODE} ====="

    echo "===== Phase 1: Varying CONNECTIONS_PER_THREAD ====="
    CONNECTIONS_PER_THREAD_VALUES=(1 4 8 12 16)
    FIXED_THREAD_COUNT=1

    run_date=$(date -u +%Y-%m-%dT%H:%M:%S%Z)

    mkdir phase1_results_"$run_date"
    cd phase1_results_"$run_date"
    for clients in "${CONNECTIONS_PER_THREAD_VALUES[@]}"; do
        run_test "${clients}" "${FIXED_THREAD_COUNT}"
    done
    cd ..

    echo "===== Phase 2: Varying THREAD_COUNT ====="
    THREAD_COUNT_VALUES=(1 8 16 24 32)
    FIXED_CONNECTIONS_PER_THREAD=8

    mkdir phase2_results_"$run_date"
    cd phase2_results_"$run_date"
    for threads in "${THREAD_COUNT_VALUES[@]}"; do
        run_test "${FIXED_CONNECTIONS_PER_THREAD}" "${threads}"
    done
    cd ..

    echo "===== Phase 3: Varying THREAD_COUNT ====="
    THREAD_COUNT_VALUES=(1 8 16 24 32)
    FIXED_CONNECTIONS_PER_THREAD=4

    mkdir phase3_results_"$run_date"
    cd phase3_results_"$run_date"
    for threads in "${THREAD_COUNT_VALUES[@]}"; do
        run_test "${FIXED_CONNECTIONS_PER_THREAD}" "${threads}"
    done
    cd ..

    echo "===== All Tests for ${MODE} Completed ====="
}

build_project
perform_tests
echo "===== All Operations Completed ====="

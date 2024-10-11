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
RUN_DURATION_SECONDS=30

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
    local alloc_pin=$1
    local enable_nagle=$2
    local increase_socket_buffers=$3
    local pin_threads=$4
    local connections_per_thread=8
    local thread_count=8
    local server_addr="172.31.45.182"

    echo "------------------------------------------------------------"
    echo "Configuration:"
    echo "  MODE: ${MODE}"
    echo "  SERVER_ADDR=${server_addr}"
    echo "  THREAD_COUNT=${thread_count}"
    echo "  CONNECTIONS_PER_THREAD=${connections_per_thread}"
    echo "  RUN_DURATION_SECONDS=${RUN_DURATION_SECONDS}"
    echo "  ALLOC_PIN=${alloc_pin}"
    echo "  ENABLE_NAGLE=${enable_nagle}"
    echo "  INCREASE_SOCKET_BUFFERS=${increase_socket_buffers}"
    echo "  PIN_THREADS=${pin_threads}"
    echo "------------------------------------------------------------"
    if [[ "$MODE" == "client" || "$MODE" == "client_s" ]]; then
        echo "Waiting 5 seconds before running this configuration..."
        sleep 5
    else
        echo "Running configuration immediately..."
    fi

    filename="alloc_pin_${alloc_pin}_enable_nagle_${enable_nagle}_increase_socket_buffers_${increase_socket_buffers}_pin_threads_${pin_threads}.log"

    sudo -E SERVER_ADDR="${server_addr}" \
           THREAD_COUNT="${thread_count}" \
           CONNECTIONS_PER_THREAD="${connections_per_thread}" \
           RUN_DURATION_SECONDS="${RUN_DURATION_SECONDS}" \
           ALLOC_PIN="${alloc_pin}" \
           ENABLE_NAGLE="${enable_nagle}" \
           INCREASE_SOCKET_BUFFERS="${increase_socket_buffers}" \
           PIN_THREADS="${pin_threads}" \
           "${EXECUTABLE}" 2>&1 | tee "${filename}"

    echo "Completed run with ALLOC_PIN=${alloc_pin}, ENABLE_NAGLE=${enable_nagle}, INCREASE_SOCKET_BUFFERS=${increase_socket_buffers}, PIN_THREADS=${pin_threads}"
    echo ""
}

perform_tests() {
    echo "===== Starting Tests for ${MODE} ====="

    local combinations=$((2**4))

    run_date=$(date -u +%Y-%m-%dT%H:%M:%S%Z)
    mkdir results_"$run_date"
    cd results_"$run_date"

    for ((i=0; i<$combinations; i++)); do
        b0=$(( (i & 8) >> 3 ))
        b1=$(( (i & 4) >> 2 ))
        b2=$(( (i & 2) >> 1 ))
        b3=$(( i & 1 ))

        alloc_pin=$b0
        enable_nagle=$b1
        increase_socket_buffers=$b2
        pin_threads=$b3

        run_test "$alloc_pin" "$enable_nagle" "$increase_socket_buffers" "$pin_threads"
    done

    cd ..

    echo "===== All Tests for ${MODE} Completed ====="
}

build_project
perform_tests
echo "===== All Operations Completed ====="

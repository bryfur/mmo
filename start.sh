#!/bin/bash

# Start the MMO server and N clients (default: 3)

# Number of clients to start (from argument or default to 3)
NUM_CLIENTS=${1:-3}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Check if executables exist
if [[ ! -f "$BUILD_DIR/mmo_server" ]]; then
    echo "Error: mmo_server not found in $BUILD_DIR"
    echo "Please build the project first with: cmake --build build"
    exit 1
fi

if [[ ! -f "$BUILD_DIR/mmo_client" ]]; then
    echo "Error: mmo_client not found in $BUILD_DIR"
    echo "Please build the project first with: cmake --build build"
    exit 1
fi

# Start the server in the background
echo "Starting server..."
"$BUILD_DIR/mmo_server" &
SERVER_PID=$!

# Give the server a moment to start
sleep 1

# Start clients
CLIENT_PIDS=()
for i in $(seq 1 $NUM_CLIENTS); do
    echo "Starting client $i..."
    "$BUILD_DIR/mmo_client" &
    CLIENT_PIDS+=($!)
done

echo ""
echo "Started:"
echo "  Server PID: $SERVER_PID"
for i in "${!CLIENT_PIDS[@]}"; do
    echo "  Client $((i+1)) PID: ${CLIENT_PIDS[$i]}"
done
echo ""
echo "Press Ctrl+C to stop all processes"

# Cleanup function
cleanup() {
    echo ""
    echo "Stopping processes..."
    for pid in "${CLIENT_PIDS[@]}"; do
        kill $pid 2>/dev/null
    done
    kill $SERVER_PID 2>/dev/null
    exit 0
}

# Trap Ctrl+C
trap cleanup SIGINT SIGTERM

# Wait for all background processes
wait

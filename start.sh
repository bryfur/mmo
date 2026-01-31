#!/bin/bash

# Start the MMO server and N clients (default: 3)

# Number of clients to start (from argument or default to 3)
NUM_CLIENTS=${1:-1}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Build the project
echo "Building..."
cmake --build "$BUILD_DIR" -j$(nproc) || { echo "Build failed"; exit 1; }

# Start the server in the background
echo "Starting server..."
"$BUILD_DIR/src/server/mmo_server" &
SERVER_PID=$!

# Give the server a moment to start
sleep 1

# Start clients
CLIENT_PIDS=()
for i in $(seq 1 $NUM_CLIENTS); do
    echo "Starting client $i..."
    "$BUILD_DIR/src/client/mmo_client" &
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

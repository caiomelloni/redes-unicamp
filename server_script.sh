#!/bin/bash

# Configuration variables
SERVER_EXECUTABLE="./server"
CLIENT_EXECUTABLE="./client"
SERVER_PORT=9871
SERVER_IP="127.0.0.1"
SLEEP_TIME=15
NUM_CLIENTS=10
MIN_BACKLOG=0
MAX_BACKLOG=10

echo "Starting Server/Client Backlog Simulation..."
echo "Server: $SERVER_EXECUTABLE"
echo "Client: $CLIENT_EXECUTABLE"
echo "Port: $SERVER_PORT | Clients: $NUM_CLIENTS | Sleep Time: $SLEEP_TIME seconds"
echo "--------------------------------------------------------"

# Loop through backlog values from 0 to 10
for BACKLOG in $(seq $MIN_BACKLOG $MAX_BACKLOG); do
    echo -e "\n--- Simulation for BACKLOG value: $BACKLOG ---"

    # 1. Execute the server in the background
    # The server runs with the current BACKLOG and specified SLEEP_TIME
    echo "Starting Server: $SERVER_EXECUTABLE $SERVER_PORT $BACKLOG $SLEEP_TIME"
    # We use 'nohup' and 'stdbuf -o L' to ensure output is not buffered and the process is robust
    # The output is redirected to a log file for that specific run
    nohup $SERVER_EXECUTABLE $SERVER_PORT $BACKLOG $SLEEP_TIME > "server_backlog_${BACKLOG}.log" 2>&1 &
    SERVER_PID=$!
    echo "Server started with PID: $SERVER_PID. (Logs: server_backlog_${BACKLOG}.log)"

    # Give the server a moment to fully start listening
    sleep 1

    # Check if the server process is running
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo "ERROR: Server failed to start with backlog $BACKLOG. Check logs and permissions."
        continue # Skip to next backlog value
    fi

    # 2. Execute 10 clients simultaneously using xargs -P
    echo "Executing $NUM_CLIENTS clients simultaneously..."
    # Prepare the client command string.
    # The 'printf' generates the command 10 times, one for each client.
    # The full command is './client 127.0.0.1 9871'
    # We use 'echo {}' as the input to xargs, which is then ignored by the client command.
    printf '%s\n' $(seq 1 $NUM_CLIENTS) | xargs -I {} -P $NUM_CLIENTS bash -c "$CLIENT_EXECUTABLE $SERVER_IP $SERVER_PORT" &
    CLIENTS_PID=$!
    
    CLIENTS_EXIT_CODE=$?
    echo "All clients finished. xargs exit code: $CLIENTS_EXIT_CODE"

    sleep 20
    # Send SIGTERM (graceful kill) and wait a moment
    kill $SERVER_PID

    echo "--------------------------------------------------------"
    
done

echo -e "\nSimulation complete."

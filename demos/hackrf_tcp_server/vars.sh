#!/bin/bash
# Example environment variables file for hackrf_tcp_server
# Usage: source vars.sh && ./hackrf_tcp_server

export BIND_ADDRESS="127.0.0.1"
export PORT="16175"
export SAMPLE_RATE="2000000"
export BITRATE="1600"
export AMPLITUDE="127"
export FREQ_DEV="2400"
export TX_GAIN="0"

echo "Environment variables set for HackRF TCP Server"
echo "BIND_ADDRESS: $BIND_ADDRESS"
echo "PORT: $PORT"
echo "You can now run: ./hackrf_tcp_server"

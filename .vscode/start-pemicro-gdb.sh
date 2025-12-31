#!/bin/bash
# Start PEMicro GDB Server for S32K142 debugging
#
# Usage: ./start-pemicro-gdb.sh [options]
#
# Options are passed directly to pegdbserver_console

PEMICRO_DIR="$HOME/.local/pemicro"
DEVICE="NXP_S32K1xx_S32K142F256M15"
INTERFACE="OPENSDA"
PORT="USB1"
SERVER_PORT="7224"
SPEED="5000"

# Set library path
export LD_LIBRARY_PATH="$PEMICRO_DIR/gdi:$PEMICRO_DIR/gdi/P&E:$LD_LIBRARY_PATH"

cd "$PEMICRO_DIR"

# Check if server is already running
if pgrep -f pegdbserver_console > /dev/null; then
    echo "PEMicro GDB Server is already running. Stopping it first..."
    pkill -f pegdbserver_console
    sleep 1
fi

echo "Starting PEMicro GDB Server..."
echo "  Device: $DEVICE"
echo "  Interface: $INTERFACE"
echo "  Port: $PORT"
echo "  Server Port: $SERVER_PORT"
echo ""

./pegdbserver_console \
    -startserver \
    -device=$DEVICE \
    -interface=$INTERFACE \
    -port=$PORT \
    -serverport=$SERVER_PORT \
    -speed=$SPEED \
    "$@"


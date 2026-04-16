#!/bin/bash
set -u

export RENODE_UART=/tmp/wolfboot.uart
export RENODE_PIDFILE=/tmp/renode.pid
export RENODE_CONFIG=tools/renode/nrf52840_wolfboot.resc
export POFF=262139
export RENODE_PORT=55155
export RENODE_OPTIONS="--pid-file=$RENODE_PIDFILE --disable-xwt -P $RENODE_PORT"
export EXPVER=tools/test-expect-version/test-expect-version
export TEST_OPTIONS=$@
export RENODE_START_TIMEOUT=30
export UART_OPEN_TIMEOUT=10
export EXPVER_TIMEOUT=75
export RENODE_LOG=/tmp/renode.log
RENODE_BG_PID=

show_renode_log() {
    if [ -f "$RENODE_LOG" ]; then
        echo "----- Renode log -----"
        tail -n 80 "$RENODE_LOG"
    fi
}

quit_renode() {
    if (which nc); then
        (echo && echo quit && echo) | nc -q 1 localhost $RENODE_PORT >/dev/null
    fi
}

renode_is_alive() {
    if [ -n "${RENODE_BG_PID:-}" ] && kill -0 "$RENODE_BG_PID" 2>/dev/null; then
        return 0
    fi
    if [ -f "$RENODE_PIDFILE" ] && kill -0 "$(cat "$RENODE_PIDFILE")" 2>/dev/null; then
        return 0
    fi
    return 1
}

wait_for_uart_node() {
    local waited=0

    while [ "$waited" -lt "$RENODE_START_TIMEOUT" ]; do
        if [ -e "$RENODE_UART" ]; then
            return 0
        fi
        if ! renode_is_alive; then
            echo "Renode exited before creating UART PTY"
            show_renode_log
            return 1
        fi
        sleep 1
        waited=$((waited + 1))
    done

    echo "Timed out waiting for Renode UART PTY: $RENODE_UART"
    show_renode_log
    return 1
}

wait_for_uart_ready() {
    local waited=0

    while [ "$waited" -lt "$UART_OPEN_TIMEOUT" ]; do
        if bash -lc 'exec 3<>"$1"' _ "$RENODE_UART" 2>/dev/null; then
            return 0
        fi
        if ! renode_is_alive; then
            echo "Renode exited before UART became ready"
            show_renode_log
            return 1
        fi
        sleep 1
        waited=$((waited + 1))
    done

    echo "Timed out waiting for Renode UART readiness: $RENODE_UART"
    show_renode_log
    return 1
}

run_expect_version() {
    local expected="$1"
    local ret

    ret=$(timeout "$EXPVER_TIMEOUT" "$EXPVER" "$RENODE_UART")
    if [ "$ret" = "$expected" ]; then
        return 0
    fi

    echo "Unexpected version from UART: got ${ret}, expected ${expected}"
    return 1
}

rm -f $RENODE_UART $RENODE_LOG

make keysclean
make keytools
make -C tools/test-expect-version
make clean && make $TEST_OPTIONS || exit 2
make /tmp/renode-test-update.bin $TEST_OPTIONS || exit 2
cp /tmp/renode-test-update.bin test-app/ || exit 3

cp wolfboot.elf /tmp/renode-wolfboot.elf || exit 3
cp test-app/image_v1_signed.bin /tmp/renode-test-v1.bin || exit 3
cp test-app/renode-test-update.bin /tmp || exit 3
echo "Launching Renode"
renode $RENODE_OPTIONS $RENODE_CONFIG >"$RENODE_LOG" 2>&1 &
RENODE_BG_PID=$!
wait_for_uart_node || { quit_renode; exit 1; }
echo "Renode up: uart port activated"
wait_for_uart_ready || { quit_renode; exit 1; }
sleep 1
echo "Renode running: renode has been started."

if run_expect_version 1; then
    echo "Factory img: OK"
else
    echo "FAILURE"
    quit_renode
    exit 1
fi

if run_expect_version 2; then
    echo "Update: OK"
else
    echo "FAILURE"
    quit_renode
    exit 1
fi

quit_renode
exit 0

#!/bin/bash

set -e

OUTPUT=$(mktemp)
PROGRAM="qemu-system-x86_64 -machine q35 -m 256 -serial file:$OUTPUT -nographic -monitor none -pflash x86_qemu_flash.bin"
EXPECTED_OUTPUT="buildroot login"
$PROGRAM &
PID=$!
TIMEOUT=15
INTERVAL=1
ELAPSED=0

while true; do
  if [[ $ELAPSED -ge $TIMEOUT ]]; then
    echo "Test failed: output string not found within timeout"
    break
  fi
  if grep -q "$EXPECTED_OUTPUT" <(cat $OUTPUT); then
    echo "Test passed!"
    break
  fi
  sleep $INTERVAL
  ELAPSED=$((ELAPSED + INTERVAL))
done

# Kill the program
kill $PID
rm $OUTPUT

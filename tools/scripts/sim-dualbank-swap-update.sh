#!/bin/bash
set -euo pipefail

if [ ! -f ".config" ]; then
    echo "Missing .config. Run make config first." >&2
    exit 1
fi

if ! grep -Eq '^(DUALBANK_SWAP(\?|)=1)' .config; then
    echo "DUALBANK_SWAP=1 is required for this simulation." >&2
    exit 1
fi

if [ ! -x "./wolfboot.elf" ]; then
    echo "wolfboot.elf not found. Build the simulator first." >&2
    exit 1
fi

rm -f sim_registers.dd

mapfile -t lines < <(./wolfboot.elf get_swap_state get_version 2>/dev/null)
if [ "${#lines[@]}" -ne 2 ] || [ "${lines[0]}" != "1" ] || [ "${lines[1]}" != "2" ]; then
    echo "Unexpected output on first boot (got: ${lines[*]-})" >&2
    exit 1
fi
echo "dualbank: first boot reports swap=${lines[0]} active_version=${lines[1]}"

mapfile -t lines < <(./wolfboot.elf success get_swap_state get_version 2>/dev/null)
if [ "${#lines[@]}" -ne 2 ] || [ "${lines[0]}" != "1" ] || [ "${lines[1]}" != "2" ]; then
    echo "Unexpected output while confirming update (got: ${lines[*]-})" >&2
    exit 1
fi
echo "dualbank: after wolfBoot_success swap=${lines[0]} active_version=${lines[1]}"

mapfile -t lines < <(./wolfboot.elf get_swap_state get_version 2>/dev/null)
if [ "${#lines[@]}" -ne 2 ] || [ "${lines[0]}" != "1" ] || [ "${lines[1]}" != "2" ]; then
    echo "Unexpected output after confirmation (got: ${lines[*]-})" >&2
    exit 1
fi
echo "dualbank: persistent swap state confirmed swap=${lines[0]} active_version=${lines[1]}"

echo "Dualbank swap simulation successful."

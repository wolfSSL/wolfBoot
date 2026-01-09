#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WOLFBOOT_ROOT="${WOLFBOOT_ROOT:-$(cd "$script_dir/../.." && pwd)}"
EMU_APPS="$WOLFBOOT_ROOT/test-app/emu-test-apps"
M33MU="${M33MU:-$(command -v m33mu || true)}"
UPDATE_SERVER_SRC="$WOLFBOOT_ROOT/tools/test-update-server/server.c"

die() {
  echo "error: $*" >&2
  exit 1
}

log() {
  echo "==> $*"
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

if [[ -z "${TARGET:-}" && -f "$WOLFBOOT_ROOT/.config" ]]; then
  TARGET="$(grep -m1 '^TARGET' "$WOLFBOOT_ROOT/.config" 2>/dev/null | sed -E 's/^TARGET[?]*=//' || true)"
fi
TARGET="${TARGET:-}"
[[ -n "${TARGET}" ]] || die "TARGET not set (export TARGET=... or set in .config)"

EMU_DIR=""
EMU_CPU=""
UART_BASE=""

get_m33mu_target() {
  case "$1" in
    stm32h563|stm32h5) echo "stm32h563" ;;
    stm32u585|stm32u5) echo "stm32u585" ;;
    stm32l552|stm32l5) echo "stm32l552" ;;
    nrf5340) echo "nrf5340" ;;
    mcxw|mcxw71) echo "mcxw71c" ;;
    *) echo "" ;;
  esac
}
case "$TARGET" in
  stm32h563|stm32h5) EMU_DIR=stm32h563; UART_BASE=40004800 ;;
  stm32u585|stm32u5) EMU_DIR=stm32u585; UART_BASE=40004800 ;;
  stm32l552|stm32l5) EMU_DIR=stm32l552; UART_BASE=40004800 ;;
  nrf5340)   EMU_DIR=nrf5340;  UART_BASE=40008000 ;;
  mcxw|mcxw71) EMU_DIR=mcxw71; UART_BASE=40038000 ;;
  *) die "unsupported TARGET=$TARGET" ;;
esac
EMU_CPU="$(get_m33mu_target "$TARGET")"
[[ -n "$EMU_CPU" ]] || die "unsupported TARGET=$TARGET (no m33mu mapping)"

need_cmd "$M33MU"
need_cmd make
need_cmd grep
need_cmd python3
need_cmd gcc

EMU_IMAGE_HEADER_SIZE="${EMU_IMAGE_HEADER_SIZE:-256}"
IMAGE_HEADER_SIZE="$EMU_IMAGE_HEADER_SIZE"
WOLFBOOT_BIN="$WOLFBOOT_ROOT/wolfboot.bin"

EMU_PATH="$EMU_APPS/$EMU_DIR"
FACTORY_IMAGE="$EMU_PATH/image_v1_signed.bin"
FACTORY_IMAGE_BASE="$EMU_PATH/image_v1_factory.bin"
UPDATE_IMAGE_V4="$EMU_PATH/image_v4_signed.bin"
UPDATE_IMAGE_V3="$EMU_PATH/image_v3_signed.bin"
UPDATE_IMAGE_V8="$EMU_PATH/image_v8_signed.bin"

STDBUF=""
if command -v stdbuf >/dev/null 2>&1; then
  STDBUF="stdbuf -o0 -e0"
fi

make -C "$EMU_APPS" TARGET="$TARGET" EMU_VERSION=1 IMAGE_HEADER_SIZE="$EMU_IMAGE_HEADER_SIZE" sign-emu
make -C "$EMU_APPS" TARGET="$TARGET" EMU_VERSION=3 IMAGE_HEADER_SIZE="$EMU_IMAGE_HEADER_SIZE" sign-emu
make -C "$EMU_APPS" TARGET="$TARGET" EMU_VERSION=4 IMAGE_HEADER_SIZE="$EMU_IMAGE_HEADER_SIZE" sign-emu
make -C "$EMU_APPS" TARGET="$TARGET" EMU_VERSION=8 IMAGE_HEADER_SIZE="$EMU_IMAGE_HEADER_SIZE" sign-emu
UPDATE_SERVER_BIN="$EMU_PATH/test-update-server"

cp -f "$FACTORY_IMAGE" "$FACTORY_IMAGE_BASE"

reset_factory_image() {
  cp -f "$FACTORY_IMAGE_BASE" "$FACTORY_IMAGE"
}

build_update_server() {
  local uart_dev="$1"
  gcc -Wall -g -ggdb -DUART_DEV="\"$uart_dev\"" -o "$UPDATE_SERVER_BIN" "$UPDATE_SERVER_SRC" -lpthread
}

log "Scenario A: factory boot"
reset_factory_image
factory_log="$EMU_PATH/factory.log"
log "factory run: boot image v1, expect UART get_version=1"
set +e
$STDBUF "$M33MU" --cpu "$EMU_CPU" --uart-stdout --no-tz \
  --boot-offset="$IMAGE_HEADER_SIZE" --timeout 2 --expect-bkpt=0x00 \
  "$FACTORY_IMAGE" >"$factory_log" 2>&1
factory_rc=$?
set -e
grep -q "get_version=1" "$factory_log" || die "factory run: expected get_version=1"
if [[ $factory_rc -ne 0 && $factory_rc -ne 127 ]]; then
  die "factory run: m33mu exited with $factory_rc"
fi
log "factory run: ok (version=1)"

wait_for_pts() {
  local log="$1"
  local base="$2"
  local pts=""
  local i
  for i in $(seq 1 100); do
    pts="$(grep "\\[UART\\] ${base} attached to" "$log" | tail -n1 | sed 's/.* //' || true)"
    if [[ -n "$pts" ]]; then
      echo "$pts"
      return 0
    fi
    sleep 0.05
  done
  return 1
}

check_update_version_in_image() {
  local image="$1"
  local expected="$2"
  local target_h="$EMU_PATH/target.h"
  local boot_addr
  local update_addr
  boot_addr="$(grep -m1 'WOLFBOOT_PARTITION_BOOT_ADDRESS' "$target_h" | sed 's/.*0x/0x/' | cut -d'u' -f1)"
  update_addr="$(grep -m1 'WOLFBOOT_PARTITION_UPDATE_ADDRESS' "$target_h" | sed 's/.*0x/0x/' | cut -d'u' -f1)"
  python3 - "$image" "$boot_addr" "$update_addr" "$IMAGE_HEADER_SIZE" "$expected" <<'PY'
import sys, struct
path, boot_s, update_s, hdr_s, expected_s = sys.argv[1:6]
boot = int(boot_s, 16)
update = int(update_s, 16)
hdr = int(hdr_s, 0)
expected = int(expected_s, 0)
off = update - boot
data = open(path, "rb").read()
if off + 8 > len(data):
    print("0")
    sys.exit(1)
magic, = struct.unpack_from("<I", data, off)
if magic != 0x464C4F57:
    print("0")
    sys.exit(1)
IMAGE_HEADER_OFFSET = 8
HDR_PADDING = 0xFF
HDR_VERSION = 0x01
p = off + IMAGE_HEADER_OFFSET
max_p = off + hdr
version = 0
while p + 4 <= max_p:
    htype = data[p] | (data[p + 1] << 8)
    if htype == 0:
        break
    if data[p] == HDR_PADDING or (p & 1):
        p += 1
        continue
    length = data[p + 2] | (data[p + 3] << 8)
    if 4 + length > (hdr - IMAGE_HEADER_OFFSET):
        break
    if p + 4 + length > max_p:
        break
    p += 4
    if htype == HDR_VERSION:
        version, = struct.unpack_from("<I", data, p)
        break
    p += length
print(version)
sys.exit(0 if version == expected else 1)
PY
}

check_pty_available() {
  python3 - <<'PY'
import pty, os
m, s = pty.openpty()
os.close(m)
os.close(s)
PY
}

run_update_scenario() {
  local label="$1"
  local update_image="$2"
  local expected_version="$3"
  local update_log="$EMU_PATH/update_${label}.log"
  local server_log="$EMU_PATH/update_server_${label}.log"
  : >"$update_log"
  check_pty_available || die "no PTY devices available (needed for ufserver)"
UPDATE_TIMEOUT="${UPDATE_TIMEOUT:-120}"
  log "$label: start emulator (expect BKPT 0x47 when update is accepted)"
  $STDBUF "$M33MU" --cpu "$EMU_CPU" --no-tz --persist \
    --boot-offset="$IMAGE_HEADER_SIZE" --timeout "$UPDATE_TIMEOUT" --expect-bkpt=0x47 \
    "$FACTORY_IMAGE" >"$update_log" 2>&1 &
  emu_pid=$!

  pts="$(wait_for_pts "$update_log" "$UART_BASE")" || die "$label: failed to detect UART PTY"
  log "$label: UART attached at $pts"
  build_update_server "$pts"
  if [[ -n "$STDBUF" ]]; then
    log "$label: transferring update image (v$expected_version) over UART"
    $STDBUF "$UPDATE_SERVER_BIN" "$update_image" >"$server_log" 2>&1 &
  else
    log "$label: transferring update image (v$expected_version) over UART"
    "$UPDATE_SERVER_BIN" "$update_image" >"$server_log" 2>&1 &
  fi
  server_pid=$!

  set +e
  wait "$emu_pid"
  emu_rc=$?
  set -e
  kill "$server_pid" >/dev/null 2>&1 || true
  wait "$server_pid" >/dev/null 2>&1 || true
  [[ $emu_rc -eq 0 ]] || die "$label: m33mu exited with $emu_rc"
  grep -q "\\[EXPECT BKPT\\] Success" "$update_log" || die "$label: expected BKPT 0x47"
  log "$label: BKPT 0x47 hit (update accepted)"
  check_update_version_in_image "$FACTORY_IMAGE" "$expected_version" || die "$label: update partition version mismatch"
  log "$label: update partition version=$expected_version"
}

log "Scenario B: successful update from v1 to v4"
reset_factory_image
run_update_scenario "scenario_b_update" "$UPDATE_IMAGE_V4" 4

for i in 1 2; do
  run_log="$EMU_PATH/reboot_v4_${i}.log"
  log "Scenario B: reboot run $i: boot updated image v4, expect BKPT 0x4A (success)"
  $STDBUF "$M33MU" --cpu "$EMU_CPU" --uart-stdout --no-tz \
    --boot-offset="$IMAGE_HEADER_SIZE" --timeout 2 --expect-bkpt=0x4a \
    "$UPDATE_IMAGE_V4" >"$run_log" 2>&1 || die "reboot v4 run $i: m33mu failed"
  grep -q "\\[EXPECT BKPT\\] Success" "$run_log" || die "reboot v4 run $i: expected BKPT 0x4A"
  log "Scenario B: reboot run $i: BKPT 0x4A hit"
done

log "Scenario C: update to v3 then fallback (no wolfBoot_success)"
reset_factory_image
run_update_scenario "scenario_c_update" "$UPDATE_IMAGE_V3" 3

log "Scenario C: reboot into v3 (expect BKPT 0x4B, no success call)"
run_log_v3="$EMU_PATH/reboot_v3.log"
$STDBUF "$M33MU" --cpu "$EMU_CPU" --uart-stdout --no-tz \
  --boot-offset="$IMAGE_HEADER_SIZE" --timeout 2 --expect-bkpt=0x4b \
  "$UPDATE_IMAGE_V3" >"$run_log_v3" 2>&1 || die "reboot v3 run: m33mu failed"
grep -q "\\[EXPECT BKPT\\] Success" "$run_log_v3" || die "reboot v3 run: expected BKPT 0x4B"
log "Scenario C: reboot v3: BKPT 0x4B hit"

log "Scenario C: reboot after fallback (expect v1)"
run_log_fallback="$EMU_PATH/reboot_fallback_v1.log"
set +e
$STDBUF "$M33MU" --cpu "$EMU_CPU" --uart-stdout --no-tz \
  --boot-offset="$IMAGE_HEADER_SIZE" --timeout 2 \
  "$FACTORY_IMAGE" >"$run_log_fallback" 2>&1
fallback_rc=$?
set -e
grep -q "get_version=1" "$run_log_fallback" || die "fallback run: expected get_version=1"
if [[ $fallback_rc -ne 0 && $fallback_rc -ne 127 ]]; then
  die "fallback run: m33mu exited with $fallback_rc"
fi
log "Scenario C: fallback ok (version=1)"

log "ok: $TARGET emu tests passed"

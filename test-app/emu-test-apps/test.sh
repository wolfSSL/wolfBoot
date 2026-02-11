#!/usr/bin/env bash

# test.sh
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WOLFBOOT_ROOT="${WOLFBOOT_ROOT:-$(cd "$script_dir/../.." && pwd)}"
EMU_APPS="$WOLFBOOT_ROOT/test-app/emu-test-apps"
M33MU="${M33MU:-$(command -v m33mu || true)}"
UPDATE_SERVER_SRC="$WOLFBOOT_ROOT/tools/test-update-server/server.c"
BIN_ASSEMBLE="$WOLFBOOT_ROOT/tools/bin-assemble/bin-assemble"

log() {
  echo "==> $*"
}

die() {
  echo "error: $*" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

cfg_get() {
  local key="$1"
  local val
  val="$(grep -m1 -E "^${key}[?]*[:]*=" "$WOLFBOOT_ROOT/.config" 2>/dev/null | sed -E "s/^${key}[?]*[:]*=//" || true)"
  echo "${val}"
}

if [[ -z "${TARGET:-}" && -f "$WOLFBOOT_ROOT/.config" ]]; then
  TARGET="$(cfg_get TARGET)"
fi
TARGET="${TARGET:-}"
[[ -n "$TARGET" ]] || die "TARGET not set (export TARGET=... or set in .config)"

EMU_DIR=""
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
  nrf5340) EMU_DIR=nrf5340; UART_BASE=40008000 ;;
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
need_cmd sed
need_cmd cut
need_cmd tac

IMAGE_HEADER_SIZE="$(cfg_get IMAGE_HEADER_SIZE)"
IMAGE_HEADER_SIZE="${IMAGE_HEADER_SIZE:-256}"
ARCH_OFFSET="$(cfg_get ARCH_OFFSET)"
ARCH_OFFSET="${ARCH_OFFSET:-0}"
BOOT_ADDR="$(cfg_get WOLFBOOT_PARTITION_BOOT_ADDRESS)"
UPDATE_ADDR="$(cfg_get WOLFBOOT_PARTITION_UPDATE_ADDRESS)"
PART_SIZE="$(cfg_get WOLFBOOT_PARTITION_SIZE)"
SECTOR_SIZE="$(cfg_get WOLFBOOT_SECTOR_SIZE)"
RAM_CODE="$(cfg_get RAM_CODE)"
TZEN="$(cfg_get TZEN)"

[[ -n "$BOOT_ADDR" && -n "$UPDATE_ADDR" && -n "$PART_SIZE" && -n "$SECTOR_SIZE" ]] || \
  die "missing required config values (boot/update addr, partition/sector size)"

# If ARCH_OFFSET is unset/0 but partitions are in 0x0800_0000 (STM32), use that
# as the base so bin-assemble offsets stay within the flash image.
if [[ "$ARCH_OFFSET" == "0" || "$ARCH_OFFSET" == "0x0" ]]; then
  if (( BOOT_ADDR >= 0x08000000 )); then
    ARCH_OFFSET=0x08000000
  fi
fi

normalize_flash_addr() {
  local addr="$1"
  if [[ "$ARCH_OFFSET" == "0x08000000" ]]; then
    if (( addr >= 0x0c000000 && addr < 0x10000000 )); then
      addr=$((addr - 0x04000000))
    fi
  fi
  echo "$addr"
}

BOOT_ADDR_NORM="$(normalize_flash_addr "$BOOT_ADDR")"
UPDATE_ADDR_NORM="$(normalize_flash_addr "$UPDATE_ADDR")"

BOOT_OFFSET=$((BOOT_ADDR_NORM - ARCH_OFFSET))
UPDATE_OFFSET=$((UPDATE_ADDR_NORM - ARCH_OFFSET))
BOOT_OFFSET_HEX=$(printf "0x%x" "$BOOT_OFFSET")
UPDATE_OFFSET_HEX=$(printf "0x%x" "$UPDATE_OFFSET")

get_check_config_val() {
  local key="$1"
  local val
  make -C "$WOLFBOOT_ROOT" include/target.h >/dev/null
  make -C "$WOLFBOOT_ROOT/tools/check_config" check_config CROSS_COMPILE=arm-none-eabi- RAM_CODE=0 >/dev/null
  val="$("$WOLFBOOT_ROOT/tools/check_config/check_config" | grep -m1 "^${key}" | sed 's/.*: *//')"
  [[ -n "$val" ]] || die "missing ${key} from tools/check_config output"
  echo "0x$val"
}

BOOT_FLAGS_ADDR="$(get_check_config_val PART_BOOT_ENDFLAGS)"
UPDATE_FLAGS_ADDR="$(get_check_config_val PART_UPDATE_ENDFLAGS)"
BOOT_FLAGS_ADDR_NORM="$(normalize_flash_addr "$BOOT_FLAGS_ADDR")"
UPDATE_FLAGS_ADDR_NORM="$(normalize_flash_addr "$UPDATE_FLAGS_ADDR")"
BOOT_FLAGS_OFFSET=$((BOOT_FLAGS_ADDR_NORM - ARCH_OFFSET))
UPDATE_FLAGS_OFFSET=$((UPDATE_FLAGS_ADDR_NORM - ARCH_OFFSET))
BOOT_FLAGS_OFFSET_HEX=$(printf "0x%x" "$BOOT_FLAGS_OFFSET")
UPDATE_FLAGS_OFFSET_HEX=$(printf "0x%x" "$UPDATE_FLAGS_OFFSET")

M33MU_TZ_ARGS=(--no-tz)
if [[ "${TZEN}" == "1" ]]; then
  M33MU_TZ_ARGS=()
fi

STDBUF=""
if command -v stdbuf >/dev/null 2>&1; then
  STDBUF="stdbuf -o0 -e0"
fi

SCENARIOS_RAW="${SCENARIOS:-}"
SCENARIOS_UP="$(echo "$SCENARIOS_RAW" | tr '[:lower:]' '[:upper:]' | tr -d ' ' | sed -e 's/,,*/,/g' -e 's/^,//' -e 's/,$//')"

want_scenario() {
  local s="$1"
  if [[ -z "$SCENARIOS_UP" ]]; then
    return 0
  fi
  echo "$SCENARIOS_UP" | grep -Eq "(^|,)$s(,|$)"
}

EMU_PATH="$EMU_APPS/$EMU_DIR"
WOLFBOOT_BIN="$WOLFBOOT_ROOT/wolfboot.bin"
FACTORY_FLASH="$EMU_PATH/factory.bin"
UPDATE_SERVER_BIN="$EMU_PATH/test-update-server"

UPDATE_IMAGE_V1="$EMU_PATH/image_v1_signed.bin"
UPDATE_IMAGE_V3="$EMU_PATH/image_v3_signed.bin"
UPDATE_IMAGE_V4="$EMU_PATH/image_v4_signed.bin"
UPDATE_IMAGE_V7="$EMU_PATH/image_v7_signed.bin"
UPDATE_IMAGE_V8="$EMU_PATH/image_v8_signed.bin"

BOOT_TIMEOUT="${BOOT_TIMEOUT:-10}"
UPDATE_TIMEOUT="${UPDATE_TIMEOUT:-10}"
REBOOT_TIMEOUT="${REBOOT_TIMEOUT:-10}"

write_target_ld() {
  local tpl=""
  local base=""
  local emu_tpl=""
  local addr
  local size
  addr=$((BOOT_ADDR + IMAGE_HEADER_SIZE))
  size=$((PART_SIZE - IMAGE_HEADER_SIZE))

  case "$TARGET" in
    stm32h563|stm32h5) base="ARM-stm32h5" ;;
    stm32u585|stm32u5) base="ARM-stm32u5" ;;
    stm32l552|stm32l5) base="ARM-stm32l5" ;;
    nrf5340) base="ARM-nrf5340" ;;
    mcxw|mcxw71) base="ARM-mcxw" ;;
    *) die "unsupported TARGET for linker template: $TARGET" ;;
  esac

  emu_tpl="$EMU_PATH/target.ld.in"
  if [[ -f "$emu_tpl" ]]; then
    sed -e "s/@FLASH_ORIGIN@/0x$(printf '%x' "$addr")/g" \
        "$emu_tpl" > "$EMU_PATH/target.ld"
    return 0
  fi

  if [[ "${TZEN}" == "1" && -f "$WOLFBOOT_ROOT/test-app/${base}-ns.ld" ]]; then
    tpl="$WOLFBOOT_ROOT/test-app/${base}-ns.ld"
  else
    tpl="$WOLFBOOT_ROOT/test-app/${base}.ld"
  fi

  [[ -f "$tpl" ]] || die "missing linker template: $tpl"

  sed -e "s/@WOLFBOOT_TEST_APP_ADDRESS@/0x$(printf '%x' "$addr")/g" \
      -e "s/@WOLFBOOT_TEST_APP_SIZE@/0x$(printf '%x' "$size")/g" \
      "$tpl" > "$EMU_PATH/target.ld"
  cat <<'SYM' >> "$EMU_PATH/target.ld"

/* Emu app startup expects these symbols. */
_estack = _end_stack;
_sidata = _stored_data;
_sdata = _start_data;
_edata = _end_data;
_sbss = _start_bss;
_ebss = _end_bss;
SYM
}

check_pty_available() {
  python3 - <<'PY'
import pty, os
m, s = pty.openpty()
os.close(m)
os.close(s)
PY
}

wait_for_pts() {
  local log_file="$1"
  local base="$2"
  local pid="$3"
  local pts=""
  local i
  for i in $(seq 1 2000); do
    pts="$(grep "\\[UART\\] ${base} attached to" "$log_file" | tail -n1 | sed 's/.* //' || true)"
    if [[ -n "$pts" ]]; then
      echo "$pts"
      return 0
    fi
    kill -0 "$pid" >/dev/null 2>&1 || break
    sleep 0.05
  done
  return 1
}

build_update_server() {
  local uart_dev="$1"
  gcc -Wall -g -ggdb -DUART_DEV="\"$uart_dev\"" -o "$UPDATE_SERVER_BIN" \
    "$UPDATE_SERVER_SRC" -lpthread
}

assemble_factory() {
  log "Assembling factory.bin from wolfboot.bin + image_v1_signed.bin"
  "$BIN_ASSEMBLE" "$FACTORY_FLASH" \
    0 "$WOLFBOOT_BIN" \
    "$BOOT_OFFSET_HEX" "$UPDATE_IMAGE_V1" >/dev/null
}

print_partition_flags() {
  local label="$1"
  python3 - "$FACTORY_FLASH" "$BOOT_FLAGS_OFFSET_HEX" "$UPDATE_FLAGS_OFFSET_HEX" "$SECTOR_SIZE" <<'PY' | sed "s/^/==> ${label}: /"
import sys
path, boot_s, update_s, sect_s = sys.argv[1:5]
boot = int(boot_s, 0)
update = int(update_s, 0)
sect = int(sect_s, 0)

def decode(state):
    if state is None:
        return "MISSING"
    if state == 0xFF:
        return "NEW(0xFF)"
    if state == 0x70:
        return "UPDATING(0x70)"
    if state == 0x10:
        return "TESTING(0x10)"
    if state == 0x00:
        return "SUCCESS(0x00)"
    return f"0x{state:02x}"

def read_state(endflags):
    try:
        with open(path, "rb") as f:
            f.seek(endflags - 4)
            magic = f.read(4)
            if magic != b"BOOT":
                return None
            f.seek(endflags - 5)
            b = f.read(1)
            if not b:
                return None
            return b[0]
    except Exception:
        return None

def read_state_alt(endflags):
    try:
        with open(path, "rb") as f:
            f.seek(endflags - sect - 4)
            magic = f.read(4)
            if magic != b"BOOT":
                return None
            f.seek(endflags - sect - 5)
            b = f.read(1)
            if not b:
                return None
            return b[0]
    except Exception:
        return None

bs0 = read_state(boot)
bs1 = read_state_alt(boot)
us0 = read_state(update)
us1 = read_state_alt(update)
print(f"boot_flags=[S0:{decode(bs0)} S1:{decode(bs1)}] update_flags=[S0:{decode(us0)} S1:{decode(us1)}]")
PY
}

print_partition_versions() {
  local label="$1"
  local boot_ver
  local update_ver
  boot_ver="$(python3 - "$FACTORY_FLASH" "$BOOT_OFFSET_HEX" "$IMAGE_HEADER_SIZE" <<'PY'
import sys, struct
path, off_s, hdr_s = sys.argv[1], sys.argv[2], sys.argv[3]
off = int(off_s, 16)
hdr = int(hdr_s, 0)
data = open(path, "rb").read()
if off + 8 > len(data):
    print("NA")
    sys.exit(0)
magic, = struct.unpack_from("<I", data, off)
if magic != 0x464C4F57:
    print("NA")
    sys.exit(0)
p = off + 8
end = off + hdr
ver = None
while p + 4 <= end:
    if data[p] == 0xFF:
        p += 1
        continue
    t = struct.unpack_from("<H", data, p)[0]
    l = struct.unpack_from("<H", data, p + 2)[0]
    p += 4
    if p + l > end:
        break
    if t == 1:
        ver = struct.unpack_from("<I", data, p)[0]
        break
    p += l
print("NA" if ver is None else ver)
PY
)"
  update_ver="$(python3 - "$FACTORY_FLASH" "$UPDATE_OFFSET_HEX" "$IMAGE_HEADER_SIZE" <<'PY'
import sys, struct
path, off_s, hdr_s = sys.argv[1], sys.argv[2], sys.argv[3]
off = int(off_s, 16)
hdr = int(hdr_s, 0)
data = open(path, "rb").read()
if off + 8 > len(data):
    print("NA")
    sys.exit(0)
magic, = struct.unpack_from("<I", data, off)
if magic != 0x464C4F57:
    print("NA")
    sys.exit(0)
p = off + 8
end = off + hdr
ver = None
while p + 4 <= end:
    if data[p] == 0xFF:
        p += 1
        continue
    t = struct.unpack_from("<H", data, p)[0]
    l = struct.unpack_from("<H", data, p + 2)[0]
    p += 4
    if p + l > end:
        break
    if t == 1:
        ver = struct.unpack_from("<I", data, p)[0]
        break
    p += l
print("NA" if ver is None else ver)
PY
)"
  log "$label: boot_version=$boot_ver update_version=$update_ver"
}

run_update_scenario() {
  local label="$1"
  local update_image="$2"
  local expected_version="$3"
  local expected_bkpt="${4:-0x47}"
  local update_log="$EMU_PATH/update_${label}.log"
  local server_log="$EMU_PATH/update_server_${label}.log"
  : >"$update_log"
  check_pty_available || die "no PTY devices available (needed for test-update-server)"
  print_partition_flags "$label: flags before"
  log "$label: start emulator (expect BKPT $expected_bkpt when update completes)"
  $STDBUF "$M33MU" --cpu "$EMU_CPU" "${M33MU_TZ_ARGS[@]}" --persist \
    --timeout "$UPDATE_TIMEOUT" --expect-bkpt="$expected_bkpt" --quit-on-faults \
    "$FACTORY_FLASH" >"$update_log" 2>&1 &
  emu_pid=$!

  pts="$(wait_for_pts "$update_log" "$UART_BASE" "$emu_pid")" || {
    tail -n 60 "$update_log" | sed 's/^/  | /'
    die "$label: failed to detect UART PTY"
  }
  log "$label: UART attached at $pts"
  build_update_server "$pts"

  log "$label: transferring update image (v$expected_version) over UART"
  $STDBUF "$UPDATE_SERVER_BIN" "$update_image" >"$server_log" 2>&1 &
  server_pid=$!

  set +e
  wait "$emu_pid"
  emu_rc=$?
  set -e

  kill "$server_pid" >/dev/null 2>&1 || true
  wait "$server_pid" >/dev/null 2>&1 || true

  if ! grep -q "\\[EXPECT BKPT\\] Success" "$update_log"; then
    tail -n 80 "$update_log" | sed 's/^/  | /'
    die "$label: expected BKPT $expected_bkpt"
  fi

  log "$label: BKPT $expected_bkpt hit"
  print_partition_flags "$label: flags after"

  if [[ $emu_rc -ne 0 && $emu_rc -ne 127 ]]; then
    die "$label: m33mu exited with $emu_rc"
  fi
}

log "Rebuilding wolfboot.bin (TZEN=${TZEN:-0})"
make -C "$WOLFBOOT_ROOT" clean wolfboot.bin

log "Building emu-test-apps images"
write_target_ld
make -C "$EMU_APPS" TARGET="$TARGET" TZEN="${TZEN:-0}" EMU_VERSION=1 IMAGE_HEADER_SIZE="$IMAGE_HEADER_SIZE" sign-emu
make -C "$EMU_APPS" TARGET="$TARGET" TZEN="${TZEN:-0}" EMU_VERSION=3 IMAGE_HEADER_SIZE="$IMAGE_HEADER_SIZE" sign-emu
make -C "$EMU_APPS" TARGET="$TARGET" TZEN="${TZEN:-0}" EMU_VERSION=4 IMAGE_HEADER_SIZE="$IMAGE_HEADER_SIZE" sign-emu
make -C "$EMU_APPS" TARGET="$TARGET" TZEN="${TZEN:-0}" EMU_VERSION=7 IMAGE_HEADER_SIZE="$IMAGE_HEADER_SIZE" sign-emu
make -C "$EMU_APPS" TARGET="$TARGET" TZEN="${TZEN:-0}" EMU_VERSION=8 IMAGE_HEADER_SIZE="$IMAGE_HEADER_SIZE" sign-emu

if want_scenario "A"; then
  assemble_factory

  log "Scenario A: factory boot"
  print_partition_flags "Scenario A: flags before factory run"
  log "factory run: boot image v1, expect UART get_version=1"
  set +e
  $STDBUF "$M33MU" --cpu "$EMU_CPU" --uart-stdout "${M33MU_TZ_ARGS[@]}" \
    --timeout "$BOOT_TIMEOUT" --quit-on-faults \
    "$FACTORY_FLASH" >"$EMU_PATH/factory.log" 2>&1
  factory_rc=$?
  set -e
  if ! grep -q "get_version=1" "$EMU_PATH/factory.log"; then
    tail -n 80 "$EMU_PATH/factory.log" | sed 's/^/  | /'
    die "factory run: expected get_version=1"
  fi
  if [[ $factory_rc -ne 0 && $factory_rc -ne 127 ]]; then
    die "factory run: m33mu exited with $factory_rc"
  fi
  log "factory run: ok (version=1)"

  log "Scenario A: receive v7 update without trigger (expect BKPT 0x4D, stay on v1)"
  assemble_factory
  run_update_scenario "scenario_a_update_v7" "$UPDATE_IMAGE_V7" 7 0x4d
  print_partition_versions "Scenario A: v7 update stored, version remains 1"
fi

if want_scenario "B"; then
  log "Scenario B: successful update from v1 to v4"
  assemble_factory
  print_partition_flags "Scenario B: flags before update"
  run_update_scenario "scenario_b_update" "$UPDATE_IMAGE_V4" 4 0x47

  for i in 1 2; do
    run_log="$EMU_PATH/reboot_v4_${i}.log"
    log "Scenario B: reboot run $i: boot updated image v4, expect BKPT 0x4A (success)"
    $STDBUF "$M33MU" --cpu "$EMU_CPU" --uart-stdout "${M33MU_TZ_ARGS[@]}" \
      --timeout "$REBOOT_TIMEOUT" --expect-bkpt=0x4a --quit-on-faults \
      "$FACTORY_FLASH" >"$run_log" 2>&1 || die "reboot v4 run $i: m33mu failed"
    grep -q "\\[EXPECT BKPT\\] Success" "$run_log" || die "reboot v4 run $i: expected BKPT 0x4A"
    log "Scenario B: reboot run $i: BKPT 0x4A hit"
   done
fi

if want_scenario "C"; then
  log "Scenario C: update from v1 to v3, then fallback (no wolfBoot_success is called)"
  assemble_factory
  print_partition_flags "Scenario C: flags before update"
  run_update_scenario "scenario_c_update" "$UPDATE_IMAGE_V3" 3 0x47

  log "Scenario C: first boot after update: reboot into v3 (expect BKPT 0x4B, no success call)"
  run_log_v3="$EMU_PATH/reboot_v3.log"
  print_partition_flags "Scenario C: flags before v3 reboot"
  $STDBUF "$M33MU" --cpu "$EMU_CPU" --uart-stdout "${M33MU_TZ_ARGS[@]}" \
    --timeout "$REBOOT_TIMEOUT" --expect-bkpt=0x4b --persist --quit-on-faults \
    "$FACTORY_FLASH" >"$run_log_v3" 2>&1 || die "reboot v3 run: m33mu failed"
  grep -q "\\[EXPECT BKPT\\] Success" "$run_log_v3" || die "reboot v3 run: expected BKPT 0x4B"
  log "Scenario C: reboot v3: BKPT 0x4B hit"
  print_partition_flags "Scenario C: flags after v3 reboot"

  log "Scenario C: second reboot, expect v1 after fallback"
  run_log_fallback="$EMU_PATH/reboot_fallback_v1.log"
  set +e
  $STDBUF "$M33MU" --cpu "$EMU_CPU" --uart-stdout "${M33MU_TZ_ARGS[@]}" \
    --timeout "$UPDATE_TIMEOUT" --persist --quit-on-faults \
    "$FACTORY_FLASH" >"$run_log_fallback" 2>&1
  fallback_rc=$?
  set -e
  print_partition_flags "Scenario C: flags after fallback run"
  print_partition_versions "Scenario C: versions after fallback run"
  if ! grep -q "get_version=1" "$run_log_fallback"; then
    tail -n 80 "$run_log_fallback" | sed 's/^/  | /'
    die "fallback run: expected get_version=1"
  fi
  if [[ $fallback_rc -ne 0 && $fallback_rc -ne 127 ]]; then
    die "fallback run: m33mu exited with $fallback_rc"
  fi
  log "Scenario C: fallback ok (version=1)"
fi

if want_scenario "S"; then
  if [[ "$RAM_CODE" == "1" ]]; then
    log "Scenario S: self-update enabled (RAM_CODE=1)"
    log "Scenario S: not enabled in this script yet"
  else
    log "Scenario S: RAM_CODE disabled, skipping"
  fi
fi

log "ok: $TARGET emu tests passed"

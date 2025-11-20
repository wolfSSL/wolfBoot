#!/bin/bash

# Automatically sets required option bytes on STM32 boards based on .config

if ! which STM32_Programmer_CLI > /dev/null 2>&1; then
    echo "Error: please make sure STM32_Programmer_CLI is installed and in your PATH." >&2
    exit 1
fi

if [ ! -f .config ]; then
    echo "Error: .config file not found. Copy a file from config/examples into .config." >&2
    exit 1
fi


TARGET="$(awk -F '?=|:=|=' '$1 == "TARGET" { print $2 }' .config)"
if [ -z "$TARGET" -o \( "$TARGET" != "stm32h5" -a "$TARGET" != "stm32l5" -a "$TARGET" != "stm32u5" \) ]; then
    echo "Error: target found in .config: '$TARGET'." >&2
    echo "This script only supports the targets stm32h5, stm32l5 and stm32u5." >&2
    exit 1
fi

TZEN="$(awk -F '?=|:=|=' '$1 == "TZEN" { print $2 }' .config)"
if [ -z "$TZEN" -o "$TZEN" != 1 ]; then
    echo "Error: this script requires a .config with TZEN=1" >&2
    exit 1
fi

FLASH_OFFSET=0x08000000

WOLFBOOT_PARTITION_BOOT_ADDRESS="$(awk -F '?=|:=|=' '$1 == "WOLFBOOT_PARTITION_BOOT_ADDRESS" { print $2 }' .config)"
WOLFBOOT_PARTITION_SIZE="$(awk -F '?=|:=|=' '$1 == "WOLFBOOT_PARTITION_SIZE" { print $2 }' .config)"
WOLFBOOT_SECTOR_SIZE="$(awk -F '?=|:=|=' '$1 == "WOLFBOOT_SECTOR_SIZE" { print $2 }' .config)"
DUALBANK_SWAP="$(awk -F '?=|:=|=' '$1 == "DUALBANK_SWAP" { print $2 }' .config)"

last_wolfboot_sector=$(((WOLFBOOT_PARTITION_BOOT_ADDRESS - FLASH_OFFSET - 1) / WOLFBOOT_SECTOR_SIZE))
last_wolfboot_sector_hex="$(printf "0x%x" "$last_wolfboot_sector")"

last_partition_sector=$((last_wolfboot_sector + WOLFBOOT_PARTITION_SIZE / WOLFBOOT_SECTOR_SIZE))
last_partition_sector_hex="$(printf "0x%x" "$last_partition_sector")"

declare -a obn # option bytes names
declare -a obv # option bytes values
oblen=0        # length of the above arrays

# add option bytes entry
function add_ob() {
    obn[$oblen]="$1"
    obv[$oblen]="$2"
    ((oblen++))
}

add_ob TZEN      0xB4
add_ob SWAP_BANK 0x0

if [ "$TARGET" = stm32h5 ]; then
    add_ob SECBOOTADD  0xC0000
    add_ob SECWM1_STRT 0x0
    add_ob SECWM1_END  $last_wolfboot_sector_hex
    add_ob SECWM2_STRT 0x0
    add_ob SECWM2_END  $last_partition_sector_hex
elif [ "$TARGET" = stm32u5 ]; then
    add_ob nSWBOOT0     0x0
    add_ob nBOOT0       0x1
    add_ob SECBOOTADD0  0xC0000
    add_ob SECWM1_PSTRT 0x0
    add_ob SECWM1_PEND  $last_wolfboot_sector_hex
    add_ob SECWM2_PSTRT 0x7f
    add_ob SECWM2_PEND  0x0
elif [ "$TARGET" = stm32l5 ]; then
    add_ob DBANK        0x1
    add_ob nSWBOOT0     0x0
    add_ob nBOOT0       0x1
    add_ob SECBOOTADD0  0x180000
    add_ob SECWM1_PSTRT 0x0
    add_ob SECWM1_PEND  $last_wolfboot_sector_hex
    add_ob SECWM2_PSTRT 0x7f
    add_ob SECWM2_PEND  0x0
fi

echo "Setting the following option bytes:"

oblist=""

for ((i = 0; i < oblen; i++)); do
    echo -e "\t${obn[$i]}" = "${obv[$i]}"
    oblist="$oblist ${obn[$i]}=${obv[$i]}"
done

echo -n "Confirm? [Y/n] "
read confirm
confirm="${confirm,,}"

[ -n "$confirm" -a "$confirm" != "y" ] && exit 0

echo Setting option bytes...

STM32_Programmer_CLI -c port=swd -ob $oblist
ret=$?

if [ $ret -eq 0 ]; then
    echo "Option bytes set successfully."
else
    echo "Failed setting option bytes."
fi

exit $ret

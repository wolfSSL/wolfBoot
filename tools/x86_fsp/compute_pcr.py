"""
Parse IFWI Image to compute initial PCR0 value as obtained by Intel Boot Guard
"""
import struct
import argparse
import hashlib
import os
import re
import subprocess

FLASH_SIZE_IN_MB = 64

def off_to_addr(off: int, image_size : int = FLASH_SIZE_IN_MB*1024*1024) -> int:
    """
    convert offset in the image to address in memory
    """
    return (4 * 1024 * 1024 * 1024) - (image_size - off)

def addr_to_off(addr:int, image_size : int = FLASH_SIZE_IN_MB*1024*1024) -> int:
    """
    convert address in memory to offset in the image
    """
    return image_size - ((4 * 1024 * 1024 * 1024) - addr)

def get_sha256_hash(data: bytearray) -> bytearray:
    """
    return the sha256 of data
    """
    h = hashlib.sha256()
    h.update(data)
    return h.digest()

def get_config_value(config: str, name):
    """
    Parse config to find line of type NAME=value, return value
    """

    pattern = rf'^{re.escape(name)}=(.*)$'
    matches = re.findall(pattern, config, re.MULTILINE)
    if matches:
        return matches[0].strip()
    return None

def get_sha256_hash_of_wolfboot_image(file_path: str):
    """
    Get sha256 hash of wolfboot image at file_path
    """
    HDR_OFF = 8
    WOLFBOOT_SHA_HDR = 0x03
    with open(file_path, 'rb') as f:
        data = f.read()
    data = data[HDR_OFF:]
    while True:
        if data[0] == 0xff:
            data = data[1:]
            continue
        t, l = struct.unpack('<HH', data[:4])
        if l == 0:
            return None
        if t == WOLFBOOT_SHA_HDR:
            return data[4:4+l]
        data = data[4+l:]

def get_keystore_sym_addr() -> int:
    """
    get the address of symbol keystore from ELF file image
    """
    symbols = subprocess.check_output(['nm', 'stage1/loader_stage1.elf']).split(b'\n')
    _start_keystore = int(list(filter(lambda x: b'_start_keystore' in x, symbols))[0].split(b' ')[0], 16)
    return _start_keystore

def pcr_extend(pcr: bytearray, data: bytearray) -> bytearray:
    """
    get value of extend operation on pcr with data
    """
    return get_sha256_hash(pcr + data)

if __name__ == '__main__':
    if os.path.exists('.config'):
        with open('.config', 'r', encoding='utf-8') as f:
            config = f.read()
    else:
        print("The file .config does not exist.")

    parser = argparse.ArgumentParser()
    parser.add_argument('image_file', type=str, help='Path to the image file')
    parser.add_argument('--target', type=str, choices=['IBG', 'qemu'],
                        default='IBG', help='Target platform')

    args = parser.parse_args()
    image = bytes()

    with open(args.image_file, 'rb') as f:
        image = bytearray(f.read())

    pcr0 = bytearray(b'\x00'*32)
    if args.target == 'qemu':
        keystore_addr = get_keystore_sym_addr()
        keystore_off = addr_to_off(keystore_addr, image_size = len(image))
        ibb = image[keystore_off:]
        h = hashlib.sha256()
        h.update(ibb)
        pcr0_data_hash = h.digest()
        pcr0 = pcr_extend(b'\x00'*32, pcr0_data_hash)

    print(f"Initial PCR0: {pcr0.hex()}")

    is_stage1_auth_enabled = get_config_value(config, 'STAGE1_AUTH') == '1'
    print(f"stage1  auth is {'enabled' if is_stage1_auth_enabled else 'disabled'}")

    if is_stage1_auth_enabled:
        fsp_s_hash = get_sha256_hash_of_wolfboot_image("src/x86/fsp_s_v1_signed.bin")
        pcr0 = pcr_extend(pcr0, fsp_s_hash)
        print(f"PCR0 after FSP_S: {pcr0.hex()}")

        wb_hash = get_sha256_hash_of_wolfboot_image('stage1/wolfboot_raw_v1_signed.bin')
        pcr0 = pcr_extend(pcr0, wb_hash)
        print(f"PCR0 after wolfboot: {pcr0.hex()}")

    # the pcrdigest needed by policy_sign tool is the hash of the concatenation of all PCRs involved in the policy.
    # we have only one PCR here
    pcr_digest = get_sha256_hash(pcr0)

    print("PCR Policy (use with tpm/policy_sign with -pcrdigest arg)")
    print(pcr_digest.hex())

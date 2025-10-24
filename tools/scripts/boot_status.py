#!/usr/bin/env python3

import argparse
import sys

# Declare config_vars as a global variable
config_vars: dict[str, int] = {}

def set_status(status_file: str, partition: str, value: str) -> None:
    with open(status_file, "r+b") as f:
        if partition == "BOOT":
            addr = config_vars["WOLFBOOT_PARTITION_BOOT_ADDRESS"]
        elif partition == "UPDATE":
            addr = config_vars["WOLFBOOT_PARTITION_UPDATE_ADDRESS"]
        else:
            print(f"Error: Invalid partition: {partition}")
            sys.exit(1)
        f.seek(addr + config_vars["WOLFBOOT_PARTITION_SIZE"] - 4)
        magic: bytes = f.read(4)
        if magic != b"BOOT":
            f.seek(addr + config_vars["WOLFBOOT_PARTITION_SIZE"] - 4)
            f.write(b"BOOT")
        if value == "NEW":
            status_byte = b"\xFF"
        elif value == "UPDATING":
            status_byte = b"\x70"
        elif value == "SUCCESS":
            status_byte = b"\x00"
        else:
            print(f"Error: Invalid value: {value}")
            sys.exit(1)
        # Write status byte at correct address
        f.seek(addr + config_vars["WOLFBOOT_PARTITION_SIZE"] - 5)
        f.write(status_byte)

def get_status(status_file: str, partition: str) -> None:
    with open(status_file, "rb") as f:
        if partition == "BOOT":
            addr = config_vars["WOLFBOOT_PARTITION_BOOT_ADDRESS"]
        elif partition == "UPDATE":
            addr = config_vars["WOLFBOOT_PARTITION_UPDATE_ADDRESS"]
        else:
            print(f"Error: Invalid partition: {partition}")
            sys.exit(1)
        f.seek(addr + config_vars["WOLFBOOT_PARTITION_SIZE"] - 4)
        magic: bytes = f.read(4)
        if magic != b"BOOT":
            print(f"Error: Missing magic at expected address {hex(addr)}")
            sys.exit(1)
        f.seek(addr + config_vars["WOLFBOOT_PARTITION_SIZE"] - 5)
        status_byte: bytes = f.read(1)
        if status_byte == b"\xFF":
            print("NEW")
        elif status_byte == b"\x70":
            print("UPDATING")
        elif status_byte == b"\x00":
            print("SUCCESS")
        else:
            print("INVALID")

def read_config(config_path: str) -> dict[str, str]:
    """
    Reads a config file and returns a dictionary of variables.
    Supports lines of the form KEY=VALUE, KEY:=VALUE, KEY::=VALUE, KEY:::=VALUE, and KEY?=VALUE.
    Ignores comments and blank lines.
    """
    config: dict[str, str]  = {}
    assignment_ops = [":::= ", "::=", ":=", "="]

    with open(config_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            for op in assignment_ops:
                if op in line:
                    parts = line.split(op, 1)
                    if len(parts) == 2:
                        key = parts[0].rstrip("?").strip()
                        value = parts[1].strip()
                        config[key] = value
                        break  # Stop after first matching operator
    return config

def main() -> None:
    parser = argparse.ArgumentParser(description="Manage boot status")
    parser.add_argument(
        "file",
        type=str,
        help="Path to the boot status file"
    )
    parser.add_argument(
        "config",
        type=str,
        help="Path to the .config file"
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    partitions = ["BOOT", "UPDATE"]
    states = ["SUCCESS", "UPDATING", "NEW"]

    set_parser = subparsers.add_parser("set")
    set_parser.add_argument("slot", choices=partitions, type=str)
    set_parser.add_argument("value", choices=states, type=str)

    get_parser = subparsers.add_parser("get")
    get_parser.add_argument("slot", choices=partitions, type=str)

    args = parser.parse_args()

    read_vars = read_config(args.config)

    # Check required config variables using a for loop
    required_vars = [
        "WOLFBOOT_PARTITION_SIZE",
        "WOLFBOOT_PARTITION_BOOT_ADDRESS",
        "WOLFBOOT_PARTITION_UPDATE_ADDRESS",
    ]
    for var in required_vars:
        if var not in read_vars:
            print(f"Error: Missing required config variable: {var}")
            sys.exit(1)
        try:
            config_vars[var] = int(read_vars[var], 16)
        except ValueError:
            print(f"Error: Config variable {var} value '{read_vars[var]}' is not a valid hex number")
            sys.exit(1)

    command: str = str(args.command)
    if command == "set":
        set_status(str(args.file), str(args.slot), str(args.value))
    elif command == "get":
        get_status(str(args.file), str(args.slot))
    else:
        parser.print_help()

if __name__ == "__main__":
    main()

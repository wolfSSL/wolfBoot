#!/usr/bin/env python
import re
import argparse

pattern = r'^([\w\.]+) +(0x[0-9a-fA-F]+) +(0x[0-9a-fA-F]+)'

def parse_map_file(map_file_path):
    components = []
    mmap = False
    flash_start = 0
    with open(map_file_path, 'r') as map_file:
        current_component = None
        for line in map_file:
            if line.startswith('FLASH'):
                m = re.match(r'FLASH +(0x[0-9a-fA-F]+)', line)
                flash_start = int(m[1], 16)
            if line == 'Linker script and memory map\n' or mmap:
                mmap = True
            else:
                continue
            # Check for start of a new component
            m = re.match(pattern, line)
            if m:
                c = {'name': m[1], 'address': int(m[2], 16), 'size': int(m[3], 16)}
                components.append(c)
    return components, flash_start

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("map_file_path", type=str,
                        help="Path to the map file",
                        nargs='?',
                        default='stage1/loader_stage1.map')
    args = parser.parse_args()
    map_file_path = args.map_file_path
    comps, flash = parse_map_file(map_file_path)
    comps = filter(lambda x: x['address'] >= flash, comps)
    comps = sorted(comps, key=lambda x: x['address'])
    print(f'Name:{"":<20}Address:{"":<20}Size:{"":<20}')
    for c in comps:
        print (f"{c['name']:<20} {hex(c['address']):<20} {hex(c['size']):<20}")
    total_sum = sum(map(lambda x: x['size'], comps))
    print(f"total sum: {hex(total_sum)} ({total_sum//1024} KB)")

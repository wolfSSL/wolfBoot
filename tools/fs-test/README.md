# fs-test: exercising the FAT32 / ext4 layer on the host

`fs-test` links the **real** parsers (`src/gpt.c`, `src/disk.c`, `src/disk_fs.c`, `src/fat32.c`, `src/ext4.c`) against a file-backed block device. Nothing is mocked except the block layer itself, so a disk image read here is parsed by exactly the code that runs in the bootloader.

This complements the unit tests rather than duplicating them. The unit tests pin behaviour against small fixtures and hostile hand-built metadata, and they must stay fast. This tool is for pointing the same code at arbitrary real volumes: large, deep, fragmented, sparse, unusual block sizes, or a real SD card you already have.

## Quick start

```sh
cd tools/fs-test
make            # build fs-test
make fixtures   # generate real mkfs volumes (no root needed)
make check      # verify every fixture reads back byte-identical
make bench      # report open/read cost per fixture
```

`make fixtures` uses `mkfs.vfat` + `mtools` and `mke2fs -d`, so it needs **no root, no loopback mounts and no sudo**. Images are sparse, so the directory costs far less than its apparent size.

## Commands

```sh
fs-test info   <image>                          # list partitions
fs-test probe  <image> <part>                   # mount, print type + geometry
fs-test cat    <image> <part> <path> [outfile]  # read a file out
fs-test verify <image> <part> <path> <ref>      # read and byte-compare
fs-test bench  <image> <part> <path> [iters]    # timed, with read counts
```

It is **read only**: `disk_write()` always fails, so the tool cannot damage an image or a device.

Pointing it at real media works too, since a block device is just a file:

```sh
sudo ./fs-test info  /dev/sdX
sudo ./fs-test probe /dev/sdX 0
sudo ./fs-test cat   /dev/sdX 0 /boot/os.itb /tmp/recovered.itb
```

## Reading the benchmark

The interesting column is **read counts**, not throughput. Host throughput is meaningless because of the page cache. On real media each read is a command round trip, so the count is what drives boot time. A raw partition needs one read for the payload; anything above that is filesystem overhead.

```
  fixture                       bytes  open/ms  read/ms  open rds  read rds
  ext4 1 MiB (4K blk)         1048576    0.003    0.050       8.0       1.0
  ext4 160 MiB multi-ext    167772160    0.008   18.067       8.0       2.0
  fat32 1 MiB 32K clus        1048576    0.004    0.056       5.0       1.0
  fat32 1 MiB                 1048576    0.023    0.078      19.0      18.0
```

ext4 costs **one read per extent**, independent of file size or cache size, because an extent describes a contiguous run. FAT32 costs one chain-walk per cluster, so small clusters are expensive. Both are visible above: the same 1 MiB file needs 1 read on a 32 KiB-cluster volume and 18 on a 512-byte-cluster one.

The metadata cache is a build knob, and it only affects the FAT32 case:

```sh
make clean && make CACHE=8192 && ./runbench.sh
```

| Cache size | FAT32 1 MiB, 512 B clusters |
| --- | --- |
| 512 B (default) | 18 reads |
| 2048 B | 6 reads |
| 8192 B | 3 reads |

## Fixtures

| Image | Covers |
| --- | --- |
| `fat32-default.img` | default cluster size (512 B here), MBR |
| `fat32-bigclus-lfn.img` | 32 KiB clusters on a 2.5 GiB volume, long filenames, nested path |
| `fat32-fragmented.img` | interleaved write/delete pattern intended to scatter clusters |
| `ext4-4k.img` | 4 KiB blocks, stock feature set, **GPT** |
| `ext4-1k.img` | 1 KiB blocks, that is `first_data_block == 1` |
| `ext4-2k.img` | 2 KiB blocks |
| `ext4-deep.img` | six-level path with a long filename |
| `ext4-multiextent.img` | 160 MiB file, which forces an extent split at 128 MiB |

Two notes on the fixtures:

- `fat32-bigclus-lfn.img` has to be over 2 GiB. FAT32 requires at least 65525 clusters by definition, so 32 KiB clusters need a large volume. A smaller one is correctly **refused** by the parser as FAT16-shaped, which is itself worth seeing.
- `fat32-fragmented.img` uses an `mcopy`/`mdel` interleave to try to scatter the target file. Whether it actually fragments depends on the mtools allocator, so check the read count against `fat32-default.img` before drawing conclusions from it.

## Big-endian coverage

`make check` also runs the fixtures as **big-endian PowerPC** under `qemu-user`, when `powerpc-linux-gnu-gcc` and `qemu-ppc-static` are installed. It skips with a note otherwise.

This matters because the parsers read every multi-byte on-media field through byte-wise accessors (`fs_le16` / `fs_le32`) rather than casting a buffer to a struct. That should make them endian-neutral, and the big-endian run proves it instead of assuming it.

The MBR/GPT layer is **deliberately bypassed** for that run, via `FSTEST_PART_OFF` / `FSTEST_PART_SZ`:

```sh
FSTEST_PART_OFF=1048576 FSTEST_PART_SZ=$SZ qemu-ppc-static ./fs-test-be \
    verify fixtures/ext4-4k.img 0 /boot/os.itb fixtures/payload/os.itb
```

`src/disk.c` and `src/gpt.c` cast packed on-disk structures and read them natively, so they are little-endian only. That is pre-existing behaviour, unrelated to the filesystem layer, and currently unreachable: no big-endian target enables disk boot. The override exists so the filesystem parsers can be tested independently of it.

The same environment variables are useful on little-endian too, for pointing the parsers at a bare filesystem image that has no partition table at all.

## Adding a case

Add a `truncate` + `mkfs` + `mcopy`/`-d` block to `mkfixtures.sh`, then a `check` line in `runtests.sh` and a `row` line in `runbench.sh`. Keep genuinely new *behaviour* in the unit tests; use this for scale, shape and real-world volumes.

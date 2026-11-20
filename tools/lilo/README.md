# lilo for wolfBoot (x86_64_lilo)

Installs the `x86_64_lilo` wolfBoot target on a legacy BIOS/MBR disk from a
LILO configuration file. See
[docs/Targets.md](../../docs/Targets.md#x86-64-legacy-biosmbr-lilo-style-x86_64_lilo)
for the boot flow and the QEMU test.

```
make -C tools/lilo
tools/lilo/lilo [-C config] [-b boot_device] [-i wolfboot.bin] [-D label]
                [-s save_file] [-t] [-v]
tools/lilo/lilo -q [-C config] [-b boot_device] [-v]
tools/lilo/lilo -u save_file [-b boot_device] [-t]
```

| Option     | Meaning                                                         |
|------------|-----------------------------------------------------------------|
| `-C file`  | configuration file, default `/etc/lilo.conf`                    |
| `-b dev`   | boot device or disk image, overrides `boot=`                    |
| `-i file`  | wolfBoot binary, overrides `install=` (default `/boot/wolfboot.bin`) |
| `-D label` | default image, overrides `default=`                             |
| `-s file`  | save the sectors about to be overwritten                        |
| `-t`       | test: check everything, write nothing                           |
| `-q`       | list the images in the installed map (`-v` for details)        |
| `-u file`  | restore the boot code saved with `-s`, keeping the partition table |
| `-v`       | verbose                                                         |
| `-V`       | version                                                         |

`lilo` writes the MBR boot code (keeping the disk signature and the partition
table), the map at LBA 1 and wolfBoot at LBA 34. All of it has to fit in front
of the first partition. GPT disks are refused.

## lilo.conf

The syntax is lilo.conf(5): `keyword=value` or a bare `keyword`, values with
blanks in double quotes, `#` comments. Options before the first `image=` or
`other=` are global.

Global options:

| Keyword          | Meaning                                                   |
|------------------|-----------------------------------------------------------|
| `boot=`          | whole-disk device or image the MBR is written to          |
| `install=`       | wolfBoot binary (`wolfboot.bin` of an `x86_64_lilo` build) |
| `default=`       | label booted by default (otherwise the first image)       |
| `prompt`         | always show the `boot:` prompt                            |
| `timeout=`       | tenths of a second at the prompt before booting the default; none means wait |
| `delay=`         | without `prompt`: tenths of a second to wait for Shift, Ctrl, Alt, Caps or Scroll Lock (or a serial character) to bring the prompt up |
| `serial=`        | `port[,bps[N8]]`: port 0-3 (COM1-COM4), also accept input from it |
| `append=`, `addappend=`, `root=`, `read-only`, `read-write`, `vga=` | defaults for the images |
| `fat-partition=` | (wolfBoot) MBR slot 1-4 holding the FAT32 boot partition; default: the first FAT32 partition |
| `fat-mount=`     | (wolfBoot) where that partition is mounted, default `/boot`. Image paths under it are looked up relative to the partition root |

Per image (`image=<path>`, a wolfBoot image of a bzImage on the FAT32
partition):

| Keyword      | Meaning                                                        |
|--------------|----------------------------------------------------------------|
| `label=`     | name typed at the prompt, at most 15 characters (default: the file name) |
| `alias=`     | second name                                                    |
| `initrd=`    | initial ramdisk on the same partition                         |
| `append=`    | kernel parameters, replacing the global `append=`             |
| `addappend=` | kernel parameters added after `append=`                       |
| `literal=`   | the only kernel parameters, ignoring `root=`, `ro`/`rw` and `append=` |
| `root=`, `read-only`, `read-write`, `vga=` | as in LILO: `vga=normal\|extended\|ask\|<mode>` |
| `optional`   | skip the image, with a warning, if its files are missing      |

The command line is built as LILO did: `BOOT_IMAGE=<label>`, `root=`,
`ro`/`rw`, then `append=` and `addappend=`. Anything typed after the label at
the prompt is added at the end.

Per partition (`other=<device>`): chain-loads the boot sector of a primary
partition, numbered from the trailing digits of the device (`/dev/sda3` is
partition 3). `label=` and `alias=` apply.

Accepted and ignored, since they describe the original map format or disk
geometry: `map`, `lba32`, `linear`, `compact`, `message`, `backup`, `disk`
sections, `table`, `unsafe` and similar. Refused, because the wolfBoot boot
loader does not implement them: `password`, `restricted`, `mandatory`,
`fallback`, `lock`, `ramdisk`, `loader`, `bitmap` and the menu options.

## Kernels

Each `image=` must be a wolfBoot image, e.g. for the `SIGN=NONE` example
configuration:

```
tools/keytools/sign --no-sign --sha256 bzImage 1
```

`lilo` checks that the file is on the FAT32 partition, is a wolfBoot image
and carries a bzImage with boot protocol 2.04 or later. At boot, wolfBoot
verifies the image again (the SHA-256 integrity check) and refuses to boot a
corrupted one, returning to the prompt.

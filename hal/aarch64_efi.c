/* aarch64_efi.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

/* Generic AArch64 UEFI-application HAL (validated on NVIDIA Jetson Orin Nano,
 * Tegra234 / Cortex-A78AE).
 *
 * wolfBoot runs here as an AArch64 UEFI application (wolfboot.efi), launched
 * by the platform UEFI firmware. It reads the kernel image from the EFI
 * Simple File System, verifies it with wolfCrypt, and hands off to it via
 * the UEFI LoadImage/StartImage services (the AArch64 Linux Image is itself
 * a PE/COFF EFI-stub application). UEFI owns DRAM/MMU/GIC state, so this HAL
 * needs no bare-metal init; the flash ops are stubs and storage/console come
 * from UEFI Boot Services. This is the AArch64 sibling of hal/x86_64_efi.c. */

#include <stdint.h>
#include <target.h>

#include "image.h"
#include "loader.h"
#include "printf.h"

#ifdef TARGET_aarch64_efi

/* -DDEBUG (object macro from DEBUG=1) collides with gnu-efi efidebug.h's
 * DEBUG(a); drop it before the EFI headers (unused here). */
#undef DEBUG

#include <efi/efi.h>
#include <efi/efilib.h>

#ifdef __WOLFBOOT
void hal_init(void)
{
}

void hal_prepare_boot(void)
{
}

#endif

#define PAGE_SIZE 0x1000
#define EFI_DEVICE_PATH_PROTOCOL_HW_TYPE 0x01
#define EFI_DEVICE_PATH_PROTOCOL_MEM_SUBTYPE 0x03

static EFI_SYSTEM_TABLE *gSystemTable;
static EFI_HANDLE gImageHandle;
EFI_PHYSICAL_ADDRESS kernel_addr;
EFI_PHYSICAL_ADDRESS update_addr;

/* Optional Linux kernel command line, read from \cmdline.txt on the ESP and
 * handed to the kernel EFI stub via LoadOptions (see aarch64_efi_do_boot). */
static CHAR16 *kernel_cmdline = NULL;
static UINTN   kernel_cmdline_bytes = 0;

int RAMFUNCTION hal_flash_write(uintptr_t address, const uint8_t *data, int len)
{
    (void)address; (void)data; (void)len;
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uintptr_t address, int len)
{
    (void)address; (void)len;
    return 0;
}

void* hal_get_primary_address(void)
{
    return (void*)kernel_addr;
}

void* hal_get_update_address(void)
{
  return (void*)update_addr;
}

void *hal_get_dts_address(void)
{
    return NULL;
}

void *hal_get_dts_update_address(void)
{
    return NULL;
}

static void panic()
{
    while(1) {}
}

#ifdef WOLFBOOT_MEASURED_BOOT_EFI_TCG2
/* EFI TCG2 protocol (TCG EFI Protocol Specification) -- not provided by gnu-efi.
 * Used to measure the wolfBoot-verified kernel into a firmware-TPM PCR via
 * HashLogExtendEvent before StartImage, extending the platform's measured-boot
 * chain to the OS. This is the same consumer pattern U-Boot uses: the platform
 * firmware / fTPM performs the hashing, PCR extend and event-log append behind
 * EFI_TCG2_PROTOCOL, so wolfBoot needs no TPM transport driver of its own. */
#define WOLFBOOT_TCG2_PROTOCOL_GUID \
  { 0x607f766c, 0x7455, 0x42be, {0x93,0x0b,0xe4,0xd7,0x6d,0xb2,0x72,0x0f} }

#define WOLFBOOT_TCG2_EV_IPL 0x0000000DUL /* boot-loader / OS measurement */

typedef struct {
    UINT8 Major;
    UINT8 Minor;
} WOLFBOOT_TCG2_VERSION;

typedef struct {
    UINT8  Size;
    WOLFBOOT_TCG2_VERSION StructureVersion;
    WOLFBOOT_TCG2_VERSION ProtocolVersion;
    UINT32  HashAlgorithmBitmap;
    UINT32  SupportedEventLogs;
    BOOLEAN TPMPresentFlag;
    UINT16  MaxCommandSize;
    UINT16  MaxResponseSize;
    UINT32  ManufacturerID;
    UINT32  NumberOfPcrBanks;
    UINT32  ActivePcrBanks;
} WOLFBOOT_TCG2_CAPABILITY;

typedef struct {
    UINT32 HeaderSize;
    UINT16 HeaderVersion;
    UINT32 PCRIndex;
    UINT32 EventType;
} __attribute__((packed)) WOLFBOOT_TCG2_EVENT_HEADER;

/* Event has a flexible array member: sizeof() is the header only; the payload
 * size is added explicitly (Event->Size = offsetof(Event) + payload). */
typedef struct {
    UINT32 Size;
    WOLFBOOT_TCG2_EVENT_HEADER Header;
    UINT8  Event[];
} __attribute__((packed)) WOLFBOOT_TCG2_EVENT;

#define WOLFBOOT_TCG2_DESC_MAX 32
/* SHA-256 bit in the TCG2 HashAlgorithmBitmap / ActivePcrBanks (EFI_TCG2 spec). */
#define WOLFBOOT_TCG2_BANK_SHA256 0x00000002u
/* Sane bounds for a device tree before hashing it (fdt v17 header is 40 bytes). */
#define WOLFBOOT_TCG2_FDT_MIN 40u
#define WOLFBOOT_TCG2_FDT_MAX (8u * 1024u * 1024u)

struct wolfboot_tcg2_protocol;
typedef struct wolfboot_tcg2_protocol WOLFBOOT_TCG2_PROTOCOL;
struct wolfboot_tcg2_protocol {
    EFI_STATUS (EFIAPI *GetCapability)(WOLFBOOT_TCG2_PROTOCOL *This,
                                       WOLFBOOT_TCG2_CAPABILITY *Cap);
    void *GetEventLog;
    EFI_STATUS (EFIAPI *HashLogExtendEvent)(WOLFBOOT_TCG2_PROTOCOL *This,
                                            UINT64 Flags,
                                            EFI_PHYSICAL_ADDRESS DataToHash,
                                            UINT64 DataToHashLen,
                                            WOLFBOOT_TCG2_EVENT *Event);
    EFI_STATUS (EFIAPI *SubmitCommand)(WOLFBOOT_TCG2_PROTOCOL *This,
                                       UINT32 InputBlockSize, UINT8 *InputBlock,
                                       UINT32 OutputBlockSize, UINT8 *OutputBlock);
    void *GetActivePcrBanks;
    void *SetActivePcrBanks;
    void *GetResultOfSetActivePcrBanks;
};

/* Locate EFI_TCG2_PROTOCOL and confirm a TPM is present. Returns NULL (and
 * logs why) if measured boot is unavailable, so callers skip it best-effort. */
static WOLFBOOT_TCG2_PROTOCOL *tcg2_open(UINT32 *activeBanks)
{
    EFI_GUID guid = WOLFBOOT_TCG2_PROTOCOL_GUID;
    WOLFBOOT_TCG2_PROTOCOL *tcg2 = NULL;
    WOLFBOOT_TCG2_CAPABILITY cap;
    EFI_STATUS status;

    *activeBanks = 0;
    status = uefi_call_wrapper(BS->LocateProtocol, 3, &guid, NULL,
                               (void **)&tcg2);
    if (status != EFI_SUCCESS || tcg2 == NULL) {
        wolfBoot_printf("TCG2: protocol not found (0x%lx); measure skipped\n",
                        (unsigned long)status);
        return NULL;
    }
    ZeroMem(&cap, sizeof(cap));
    cap.Size = (UINT8)sizeof(cap);
    status = uefi_call_wrapper(tcg2->GetCapability, 2, tcg2, &cap);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("TCG2: GetCapability failed (0x%lx)\n",
                        (unsigned long)status);
        return NULL;
    }
    wolfBoot_printf("TCG2: TPM present=%d activeBanks=0x%x banks=%d\n",
                    (int)cap.TPMPresentFlag, (unsigned)cap.ActivePcrBanks,
                    (int)cap.NumberOfPcrBanks);
    if (!cap.TPMPresentFlag) {
        wolfBoot_printf("TCG2: no firmware TPM present; measure skipped\n");
        return NULL;
    }
    *activeBanks = cap.ActivePcrBanks;
    return tcg2;
}

/* Extend one measurement (addr,size) into PCR WOLFBOOT_MEASURED_PCR_A, tagging
 * the event-log entry with a short ASCII description. */
static void tcg2_extend(WOLFBOOT_TCG2_PROTOCOL *tcg2, const void *addr,
                        uint32_t size, const char *desc)
{
    uint8_t buf[__builtin_offsetof(WOLFBOOT_TCG2_EVENT, Event)
                + WOLFBOOT_TCG2_DESC_MAX];
    WOLFBOOT_TCG2_EVENT *evt = (WOLFBOOT_TCG2_EVENT *)buf;
    EFI_STATUS status;
    UINTN desclen = 0;

    while (desc[desclen] != '\0' && desclen < WOLFBOOT_TCG2_DESC_MAX - 1)
        desclen++;
    desclen++; /* include the NUL terminator in the event data */

    ZeroMem(buf, sizeof(buf));
    evt->Size = (UINT32)(__builtin_offsetof(WOLFBOOT_TCG2_EVENT, Event) + desclen);
    evt->Header.HeaderSize = (UINT32)sizeof(WOLFBOOT_TCG2_EVENT_HEADER);
    evt->Header.HeaderVersion = 1;
    evt->Header.PCRIndex = (UINT32)WOLFBOOT_MEASURED_PCR_A;
    evt->Header.EventType = (UINT32)WOLFBOOT_TCG2_EV_IPL;
    CopyMem(evt->Event, (void *)desc, desclen);

    status = uefi_call_wrapper(tcg2->HashLogExtendEvent, 5, tcg2, (UINT64)0,
                               (EFI_PHYSICAL_ADDRESS)(uintptr_t)addr,
                               (UINT64)size, evt);
    /* %a: desc is an ASCII string; wolfBoot_printf maps to gnu-efi Print where
     * %s is CHAR16 and %a is CHAR8. */
    if (status != EFI_SUCCESS)
        wolfBoot_printf("TCG2: extend '%a' failed 0x%lx\n", desc,
                        (unsigned long)status);
    else
        wolfBoot_printf("TCG2: measured %a (%u bytes) into PCR %d\n",
                        desc, size, (int)WOLFBOOT_MEASURED_PCR_A);
}

/* Read back the resulting PCR (SHA-256 bank) via a raw TPM2_PCR_Read submitted
 * through TCG2 SubmitCommand, and print it -- the attestable value a verifier
 * can compare against the event log. */
static void tcg2_read_pcr(WOLFBOOT_TCG2_PROTOCOL *tcg2, uint32_t pcr,
                          UINT32 activeBanks)
{
    static const char hc[] = "0123456789abcdef";
    uint8_t cmd[20];
    uint8_t resp[128];
    char hex[33];
    EFI_STATUS status;
    UINTN i, row;
    const uint8_t *d;

    /* TPM2 defines PCRs 0-23; the 3-byte selection below can only address those,
     * and cmd[17 + pcr/8] would otherwise overflow the command buffer. */
    if (pcr > 23) {
        wolfBoot_printf("TCG2: PCR %d out of range (0-23); read skipped\n",
                        (int)pcr);
        return;
    }

    /* This read-back requests the SHA-256 bank; skip it (rather than report a
     * misleading failure) if the TPM has no SHA-256 bank active. The
     * measurements above still extended whatever banks are active. */
    if (!(activeBanks & WOLFBOOT_TCG2_BANK_SHA256)) {
        wolfBoot_printf("TCG2: SHA-256 PCR bank not active; PCR read skipped\n");
        return;
    }

    ZeroMem(cmd, sizeof(cmd));
    cmd[0] = 0x80; cmd[1] = 0x01;              /* TPM_ST_NO_SESSIONS */
    cmd[5] = 20;                               /* commandSize */
    cmd[8] = 0x01; cmd[9] = 0x7E;              /* TPM_CC_PCR_Read */
    cmd[13] = 1;                               /* pcrSelectionIn count = 1 */
    cmd[15] = 0x0B;                            /* TPM_ALG_SHA256 */
    cmd[16] = 3;                               /* sizeofSelect */
    cmd[17 + (pcr / 8)] = (uint8_t)(1u << (pcr % 8)); /* select the PCR bit */

    ZeroMem(resp, sizeof(resp));
    status = uefi_call_wrapper(tcg2->SubmitCommand, 5, tcg2,
                               (UINT32)sizeof(cmd), cmd,
                               (UINT32)sizeof(resp), resp);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("TCG2: PCR read submit failed 0x%lx\n",
                        (unsigned long)status);
        return;
    }
    /* responseCode (offset 6, 4 bytes) == 0 and digest size (offset 28) == 32 */
    if (resp[6] || resp[7] || resp[8] || resp[9] ||
        resp[28] != 0x00 || resp[29] != 0x20) {
        wolfBoot_printf("TCG2: PCR read unexpected response\n");
        return;
    }
    d = resp + 30; /* TPM2B_DIGEST data (32-byte SHA-256) */
    /* Print in two 16-byte rows: the UEFI serial console truncates at 80
     * columns, so the full 64-char digest on one line would be cut off. */
    wolfBoot_printf("TCG2: PCR %d (SHA256):\n", (int)pcr);
    for (row = 0; row < 2; row++) {
        for (i = 0; i < 16; i++) {
            hex[i * 2]     = hc[(d[row * 16 + i] >> 4) & 0xf];
            hex[i * 2 + 1] = hc[d[row * 16 + i] & 0xf];
        }
        hex[32] = '\0';
        wolfBoot_printf("  %a\n", hex); /* %a: ASCII */
    }
}

/* Measure the platform device tree the firmware published in the UEFI
 * configuration table (the kernel boots against it), extending the OS
 * measurement beyond the kernel image alone. */
static void tcg2_measure_dtb(WOLFBOOT_TCG2_PROTOCOL *tcg2)
{
    UINTN i;
    uint32_t totalsize;
    const uint8_t *p;

    /* The platform publishes its device tree in a UEFI configuration table
     * (the kernel boots against it -- "Using DTB from configuration table").
     * Rather than trust a specific vendor GUID (which varies), scan the tables
     * for the entry whose contents start with the FDT magic (0xd00dfeed). */
    for (i = 0; i < gSystemTable->NumberOfTableEntries; i++) {
        p = (const uint8_t *)gSystemTable->ConfigurationTable[i].VendorTable;
        if (p == NULL)
            continue;
        if (p[0] == 0xd0 && p[1] == 0x0d && p[2] == 0xfe && p[3] == 0xed) {
            totalsize = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) |
                        ((uint32_t)p[6] << 8)  | (uint32_t)p[7];
            /* Bound the size before hashing: a corrupt header or a false magic
             * match must not send HashLogExtendEvent over a huge/invalid range.
             * Keep scanning in case another table holds a valid FDT. */
            if (totalsize < WOLFBOOT_TCG2_FDT_MIN ||
                totalsize > WOLFBOOT_TCG2_FDT_MAX) {
                wolfBoot_printf("TCG2: FDT size %u out of range; skipped\n",
                                (unsigned)totalsize);
                continue;
            }
            tcg2_extend(tcg2, p, totalsize, "wolfBoot dtb");
            return;
        }
    }
    wolfBoot_printf("TCG2: no valid FDT in config tables; dtb measure skipped\n");
}

/* Measure the full OS input set -- kernel image, kernel command line and the
 * platform device tree -- into the firmware TPM, then read the PCR back. All
 * best-effort; a missing TPM or item is logged and skipped, never fatal. */
static void tcg2_measure_all(const void *kernel, uint32_t kernel_size)
{
    UINT32 activeBanks = 0;
    WOLFBOOT_TCG2_PROTOCOL *tcg2 = tcg2_open(&activeBanks);

    if (tcg2 == NULL)
        return;
    tcg2_extend(tcg2, kernel, kernel_size, "wolfBoot kernel.img");
    if (kernel_cmdline != NULL && kernel_cmdline_bytes > 0)
        tcg2_extend(tcg2, kernel_cmdline, (uint32_t)kernel_cmdline_bytes,
                    "wolfBoot cmdline");
    tcg2_measure_dtb(tcg2);
    tcg2_read_pcr(tcg2, (uint32_t)WOLFBOOT_MEASURED_PCR_A, activeBanks);
}
#endif /* WOLFBOOT_MEASURED_BOOT_EFI_TCG2 */

void RAMFUNCTION aarch64_efi_do_boot(const uint32_t *boot_addr)
{
    const uint32_t *size;
    const uint8_t* manifest = ((const uint8_t*)boot_addr) - IMAGE_HEADER_SIZE;
    MEMMAP_DEVICE_PATH mem_path_device[2];
    EFI_HANDLE kernelImageHandle;
    EFI_STATUS status;
    EFI_LOADED_IMAGE *kernel_li = NULL;
    EFI_GUID lipGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

    size = (const uint32_t *)(manifest + 4);

    /* Guard against a zero-size image: EndingAddress below would underflow and
     * an empty range would be handed to LoadImage (and the TCG2 measurement). */
    if (*size == 0) {
        wolfBoot_printf("invalid zero-size image\n");
        panic();
    }

    mem_path_device->Header.Type = EFI_DEVICE_PATH_PROTOCOL_HW_TYPE;
    mem_path_device->Header.SubType = EFI_DEVICE_PATH_PROTOCOL_MEM_SUBTYPE;
    mem_path_device->MemoryType = EfiLoaderData;
    mem_path_device->StartingAddress = (EFI_PHYSICAL_ADDRESS)(uintptr_t)boot_addr;
    /* MEMMAP_DEVICE_PATH EndingAddress is inclusive (last valid byte). */
    mem_path_device->EndingAddress =
        (EFI_PHYSICAL_ADDRESS)((uintptr_t)boot_addr + *size - 1);
    SetDevicePathNodeLength(&mem_path_device->Header,
                            sizeof(MEMMAP_DEVICE_PATH));

    SetDevicePathEndNode(&mem_path_device[1].Header);

#ifdef WOLFBOOT_MEASURED_BOOT_EFI_TCG2
    /* Measure kernel + cmdline + DTB into the firmware TPM before handoff. */
    tcg2_measure_all(boot_addr, *size);
#endif

    wolfBoot_printf("Staging kernel at address %p, size: %u\n", (void*)boot_addr, *size);
    status = uefi_call_wrapper(gSystemTable->BootServices->LoadImage,
                               6,
                               0, /* bool */
                               gImageHandle,
                               (EFI_DEVICE_PATH*)mem_path_device,
                               (void*)(uintptr_t)boot_addr,
                               *size,
                               &kernelImageHandle);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("LoadImage failed: 0x%lx\n", (unsigned long)status);
        panic();
    }

    /* Pass the kernel command line (from \cmdline.txt) to the loaded image via
     * LoadOptions for the Linux EFI stub. For a production trust chain the
     * cmdline should be authenticated (in the signed image or DT /chosen), not
     * a plaintext file. A direct root= boot needs no initrd; an initramfs flow
     * would add it via LINUX_EFI_INITRD_MEDIA_GUID (LoadFile2). */
    if (kernel_cmdline != NULL) {
        status = uefi_call_wrapper(gSystemTable->BootServices->HandleProtocol, 3,
                                   kernelImageHandle, &lipGuid,
                                   (void**)&kernel_li);
        if (status == EFI_SUCCESS && kernel_li != NULL) {
            kernel_li->LoadOptions = kernel_cmdline;
            kernel_li->LoadOptionsSize = (UINT32)kernel_cmdline_bytes;
        }
    }

    status = uefi_call_wrapper(gSystemTable->BootServices->StartImage,
                               3,
                               kernelImageHandle, 0, NULL);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("StartImage failed: 0x%lx\n", (unsigned long)status);
        panic();
    }
}

static UINT64 FileSize(EFI_FILE_HANDLE FileHandle)
{
    EFI_FILE_INFO *FileInfo;
    UINT64 ret;

    FileInfo = LibFileInfo(FileHandle);
    if (FileInfo == NULL) {
        panic();
        return 0; /* Never reached, for static analyzer */
    }

    ret = FileInfo->FileSize;
    FreePool(FileInfo);

    return ret;
}

static EFI_FILE_HANDLE GetVolume(EFI_HANDLE image)
{
    EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID lipGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE *loaded_image = NULL;
    EFI_FILE_IO_INTERFACE *IOVolume;
    EFI_FILE_HANDLE Volume;
    EFI_STATUS status;

    status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               image, &lipGuid, (void **) &loaded_image);
    if (status != EFI_SUCCESS)
        panic();

    status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               loaded_image->DeviceHandle,
                               &fsGuid, (VOID*)&IOVolume);
    if (status != EFI_SUCCESS)
        panic();

    status = uefi_call_wrapper(IOVolume->OpenVolume, 2, IOVolume, &Volume);

    if (status != EFI_SUCCESS)
        panic();

    return Volume;
}

static EFI_FILE_HANDLE openFile(CHAR16 *file, EFI_FILE_HANDLE volume)
{
    EFI_FILE_HANDLE file_handle;
    EFI_STATUS status;

    /* Open() ignores Attributes for read-only access (they only apply with
     * EFI_FILE_MODE_CREATE); pass 0 so stricter firmware does not reject it. */
    status = uefi_call_wrapper(volume->Open, 5,
                               volume,
                               &file_handle,
                               file,
                               EFI_FILE_MODE_READ,
                               0);

    if (status != EFI_SUCCESS)
        file_handle = NULL;

    return file_handle;
}

static int open_kernel_image(EFI_FILE_HANDLE vol, CHAR16 *filename,
        EFI_PHYSICAL_ADDRESS *_addr, uint32_t *sz)
{
    EFI_FILE_HANDLE file;
    EFI_STATUS status;
    UINTN readsz;
    UINTN pages;

    /* Always leave *_addr well-defined so the caller's "no image" check is
     * reliable: 0 on any failure, the loaded address only on full success. */
    *_addr = 0;
    *sz = 0;
    file = openFile(filename, vol);
    if (file == NULL)
        return -1;

    *sz =  FileSize(file);
    pages = (*sz / PAGE_SIZE) + 1;
    wolfBoot_printf("Opening file: %s, size: %u\n", filename, *sz);
    status = uefi_call_wrapper(BS->AllocatePages,
                          4,
                          AllocateAnyPages,
                          EfiLoaderData,
                          pages, _addr);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("AllocatePages failed: 0x%lx\n", (unsigned long)status);
        *_addr = 0;
        uefi_call_wrapper(file->Close, 1, file);
        return status;
    }

    /* EFI_FILE Read() takes UINTN *BufferSize and VOID *Buffer. On AArch64
     * uefi_call_wrapper is a native passthrough (no arg casting), so pass the
     * correct types explicitly rather than a uint32_t pointer and an integer
     * address. */
    readsz = (UINTN)*sz;
    status = uefi_call_wrapper(file->Read, 3, file, &readsz,
                               (void*)(uintptr_t)*_addr);
    *sz = (uint32_t)readsz;
    uefi_call_wrapper(file->Close, 1, file); /* done with the file */
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("file Read failed: 0x%lx\n", (unsigned long)status);
        uefi_call_wrapper(BS->FreePages, 2, *_addr, pages);
        *_addr = 0;
        return status;
    }

    if (*sz < IMAGE_HEADER_SIZE) {
        wolfBoot_printf("Image smaller than the header\n");
        uefi_call_wrapper(BS->FreePages, 2, *_addr, pages);
        *_addr = 0;
        return -1;
    }

    return 0;
}

/* Read an optional \cmdline.txt (ASCII) from the ESP into a widechar buffer
 * for the kernel LoadOptions. No-op (leaves kernel_cmdline NULL) if absent. */
static void read_cmdline(EFI_FILE_HANDLE vol)
{
    EFI_FILE_HANDLE file;
    EFI_STATUS status;
    UINT64 sz;
    UINTN readsz, i, n;
    uint8_t *ascii = NULL;
    CHAR16 *wide = NULL;

    file = openFile(L"cmdline.txt", vol);
    if (file == NULL)
        return; /* optional */

    sz = FileSize(file);
    if (sz == 0 || sz > 4096) {
        uefi_call_wrapper(file->Close, 1, file);
        return;
    }

    status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
                               (UINTN)sz, (void**)&ascii);
    if (status != EFI_SUCCESS || ascii == NULL) {
        uefi_call_wrapper(file->Close, 1, file);
        return;
    }

    readsz = (UINTN)sz;
    status = uefi_call_wrapper(file->Read, 3, file, &readsz, ascii);
    uefi_call_wrapper(file->Close, 1, file); /* done with the file */
    if (status != EFI_SUCCESS) {
        FreePool(ascii);
        return;
    }

    /* trim trailing CR/LF/whitespace */
    n = (UINTN)readsz;
    while (n > 0 && (ascii[n-1] == '\n' || ascii[n-1] == '\r' ||
                     ascii[n-1] == ' '  || ascii[n-1] == '\t'))
        n--;

    status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
                               (n + 1) * sizeof(CHAR16), (void**)&wide);
    if (status != EFI_SUCCESS || wide == NULL) {
        FreePool(ascii);
        return;
    }
    for (i = 0; i < n; i++)
        wide[i] = (CHAR16)ascii[i];
    wide[n] = 0;
    FreePool(ascii);

    kernel_cmdline = wide;
    kernel_cmdline_bytes = (n + 1) * sizeof(CHAR16);
    wolfBoot_printf("Kernel cmdline (%d chars) from cmdline.txt\n", (int)n);
}

EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    CHAR16 *kernel_filename = L"kernel.img";
    CHAR16 *update_filename = L"update.img";
    EFI_LOADED_IMAGE *loaded_image = NULL;
    EFI_FILE_HANDLE vol;
    EFI_STATUS status;
    uint32_t kernel_size, update_size;

    InitializeLib(ImageHandle, SystemTable);
    gSystemTable = SystemTable;
    gImageHandle = ImageHandle;

    status = uefi_call_wrapper(SystemTable->BootServices->HandleProtocol,
                               3,
                               ImageHandle,
                               &LoadedImageProtocol,
                               (void **)&loaded_image);

    if (status == EFI_SUCCESS)
        wolfBoot_printf("Image base: 0x%lx\n", loaded_image->ImageBase);
    vol = GetVolume(ImageHandle);
    read_cmdline(vol);
    /* open_kernel_image() leaves *_addr == 0 on failure (and frees any pages),
     * so the "no image" check below is reliable; the sizes are logged inside
     * and not needed here. */
    (void)open_kernel_image(vol, kernel_filename, &kernel_addr, &kernel_size);
    (void)open_kernel_image(vol, update_filename, &update_addr, &update_size);
    (void)kernel_size;
    (void)update_size;

    if (kernel_addr == 0 && update_addr == 0) {
        wolfBoot_printf("No image to load\n");
        panic();
    }

    wolfBoot_start();

    return EFI_SUCCESS;
}

#endif /* TARGET_aarch64_efi */

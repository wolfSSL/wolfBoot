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

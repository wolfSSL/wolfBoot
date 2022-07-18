
/* x86_64_efi.c
 *
 * Copyright (C) 2021 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include <stdint.h>
#include <target.h>

#include "image.h"
#include "loader.h"
#include "printf.h"

#ifdef PLATFORM_X86_64_EFI

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
static EFI_HANDLE *gImageHandle;
EFI_PHYSICAL_ADDRESS kernel_addr;
EFI_PHYSICAL_ADDRESS update_addr;

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    return 0;
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
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


static void panic()
{
    while(1) {}
}

void RAMFUNCTION x86_64_efi_do_boot(uint32_t *boot_addr, uint8_t *dts_address)
{
    uint32_t *size;
    uint8_t* manifest = ((uint8_t*)boot_addr) - IMAGE_HEADER_SIZE;

    (void)dts_address; /* Unused for now */

    MEMMAP_DEVICE_PATH mem_path_device[2];
    EFI_HANDLE kernelImageHandle;
    EFI_STATUS status;

    size = (uint32_t *)(manifest + 4);

    mem_path_device->Header.Type = EFI_DEVICE_PATH_PROTOCOL_HW_TYPE;
    mem_path_device->Header.SubType = EFI_DEVICE_PATH_PROTOCOL_MEM_SUBTYPE;
    mem_path_device->MemoryType = EfiLoaderData;
    mem_path_device->StartingAddress = (EFI_PHYSICAL_ADDRESS)boot_addr;
    mem_path_device->EndingAddress = (EFI_PHYSICAL_ADDRESS)(boot_addr+*size);
    SetDevicePathNodeLength(&mem_path_device->Header,
                            sizeof(MEMMAP_DEVICE_PATH));

    SetDevicePathEndNode(&mem_path_device[1].Header);

    wolfBoot_printf("Staging kernel at address %x, size: %u\n", boot_addr, *size);
    status = uefi_call_wrapper(gSystemTable->BootServices->LoadImage,
                               6,
                               0, /* bool */
                               gImageHandle,
                               (EFI_DEVICE_PATH*)mem_path_device,
                               boot_addr,
                               *size,
                               &kernelImageHandle);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("can't load kernel image from memory\n");
        panic();
    }

    status = uefi_call_wrapper(gSystemTable->BootServices->StartImage,
                               3,
                               kernelImageHandle, 0, NULL);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("can't load kernel image from memory\n");
        panic();
    }
}

static UINT64 FileSize(EFI_FILE_HANDLE FileHandle)
{
    EFI_FILE_INFO *FileInfo;
    UINT64 ret;

    FileInfo = LibFileInfo(FileHandle);
    if (FileInfo == NULL)
        panic();

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

    status = uefi_call_wrapper(volume->Open, 5,
                               volume,
                               &file_handle,
                               file,
                               EFI_FILE_MODE_READ,
                               EFI_FILE_READ_ONLY | EFI_FILE_HIDDEN | EFI_FILE_SYSTEM);

    if (status != EFI_SUCCESS)
        file_handle = NULL;

    return file_handle;
}

static int open_kernel_image(EFI_FILE_HANDLE vol, CHAR16 *filename,
        EFI_PHYSICAL_ADDRESS *_addr, uint32_t *sz)
{
    EFI_FILE_HANDLE file;
    EFI_STATUS status;

    file = openFile(filename, vol);
    if (file == NULL)
        return -1;

    *sz =  FileSize(file);
    wolfBoot_printf("Opening file: %s, size: %u\n", filename, *sz);
    status = uefi_call_wrapper(BS->AllocatePages,
                          4,
                          AllocateAnyPages,
                          EfiLoaderData,
                          (*sz/PAGE_SIZE) + 1, _addr);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("can't get memory at specified address %d\n", status);
        return status;
    }

    status = uefi_call_wrapper(file->Read, 3, file, sz, *_addr);
    if (status != EFI_SUCCESS) {
        wolfBoot_printf("can't read kernel image %d\n", status);
        return status;
    }

    if (*sz < IMAGE_HEADER_SIZE) {
        wolfBoot_printf("Image smaller than the header\n");
        return -1;
    }

    return 0;
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
    uint8_t *mem;
    UINT64 size;
    UINT64 r;
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
    open_kernel_image(vol, kernel_filename, &kernel_addr, &kernel_size);
    open_kernel_image(vol, update_filename, &update_addr, &update_size);

    if (kernel_addr == 0 && update_addr == 0) {
        wolfBoot_printf("No image to load\n");
        panic();
    }

    wolfBoot_start();

    return EFI_SUCCESS;
}

#endif /* PLATFORM_X86_64_EFI */

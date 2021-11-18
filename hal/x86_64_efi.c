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

#ifdef PLATFORM_X86_64_EFI

#include <efi/efi.h>
#include <efi/efilib.h>

#ifdef __WOLFBOOT
void hal_init(void)
{
}

void hal_prepare_boot(void)
{
    Print(L"hal_prepare_boot\n");
    /* ExitBootServices() */
}

#endif

#define PAGE_SIZE 0x1000

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


UINT64 FileSize(EFI_FILE_HANDLE FileHandle)
{
    UINT64 ret;
    EFI_FILE_INFO       *FileInfo;         /* file information structure */
    /* get the file's size */
    FileInfo = LibFileInfo(FileHandle);
    ret = FileInfo->FileSize;
    FreePool(FileInfo);
    return ret;
}

EFI_FILE_HANDLE GetVolume(EFI_HANDLE image)
{
    EFI_LOADED_IMAGE *loaded_image = NULL;                  /* image interface */
    EFI_GUID lipGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;      /* image interface GUID */
    EFI_FILE_IO_INTERFACE *IOVolume;                        /* file system interface */
    EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID; /* file system interface GUID */
    EFI_FILE_HANDLE Volume;                                 /* the volume's interface */

    /* get the loaded image protocol interface for our "image" */
    uefi_call_wrapper(BS->HandleProtocol, 3, image, &lipGuid, (void **) &loaded_image);
    /* get the volume handle */
    uefi_call_wrapper(BS->HandleProtocol, 3, loaded_image->DeviceHandle, &fsGuid, (VOID*)&IOVolume);
    uefi_call_wrapper(IOVolume->OpenVolume, 2, IOVolume, &Volume);
    return Volume;
}

EFI_FILE_HANDLE openFile(CHAR16 *file, EFI_FILE_HANDLE volume)
{
    EFI_FILE_HANDLE file_handle;

    uefi_call_wrapper(volume->Open, 5,
                      volume,
                      &file_handle,
                      file,
                      EFI_FILE_MODE_READ,
                      EFI_FILE_READ_ONLY | EFI_FILE_HIDDEN | EFI_FILE_SYSTEM);

    return file_handle;
}

EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    CHAR16 *filename = L"vmlinuz-linux_v1_signed.bin";
    EFI_LOADED_IMAGE *loaded_image = NULL;
    EFI_PHYSICAL_ADDRESS kernel_addr;
    EFI_FILE_HANDLE vol, file;
    EFI_STATUS status;
    uint8_t *mem;
    UINT64 size;
    UINT64 r;

    InitializeLib(ImageHandle, SystemTable);
    Print(L"HERE %d\n", __LINE__);

    status = uefi_call_wrapper(SystemTable->BootServices->HandleProtocol,
                               3,
                               ImageHandle,
                               &LoadedImageProtocol,
                               (void **)&loaded_image);

    Print(L"Image base: 0x%lx\n", loaded_image->ImageBase);


    vol = GetVolume(ImageHandle);
    file = openFile(filename, vol);
    size =  FileSize(file);

    Print(L"size is %ld\n", size);

    kernel_addr = WOLFBOOT_PARTITION_BOOT_ADDRESS;

    r = uefi_call_wrapper(BS->AllocatePages,
                          4,
                          AllocateAnyPages,
                          EfiLoaderData,
                          (size/PAGE_SIZE) + 1, &kernel_addr);

    Print(L"ret is 0x%lx\n", r);
    Print(L"mem is 0x%lx\n", kernel_addr);

    mem = (uint8_t*)kernel_addr;

    r = uefi_call_wrapper(file->Read, 3, file, &size, mem);
    Print(L"file read ret 0x%lx\n", r);

    Print(L"%lx\n\n", ((uint64_t*)mem)[0]);

     wolfBoot_start();

    return EFI_SUCCESS;
}

#endif /* PLATFORM_X86_64_EFI */

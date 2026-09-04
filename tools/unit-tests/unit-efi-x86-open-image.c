/* unit-efi-x86-open-image.c
 *
 * Regression test: open_kernel_image() in hal/x86_64_efi.c passed the
 * caller's uint32_t *sz as the BufferSize argument of
 * EFI_FILE_PROTOCOL.Read(). That is a UINTN -- 64 bits on x86-64 --
 * and the firmware writes the byte count back through it, so every
 * successful read stored eight bytes through a four-byte object and
 * corrupted the stack after it. The fix passes a local UINTN and copies
 * the count back, as the AArch64 sibling does.
 *
 * hal/x86_64_efi.c is normally built only by the CMake x86_64_efi
 * target, so this doubles as its host build coverage: it includes the
 * HAL directly and mocks the gnu-efi runtime. The mock Read() follows
 * the spec'd BufferSize contract, so it fails against the pre-fix code.
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

#include <check.h>
#include <stdint.h>
#include <stdio.h> /* loader.h (pulled in via the HAL) uses fprintf */
#include <stdlib.h>
#include <string.h>

/* gnu-efi headers first, same order as the CMake x86_64_efi target and the
 * HAL file itself. */
#include <efi/efi.h>
#include <efi/efilib.h>

/* ------------------------------------------------------------------ */
/* Host stand-ins for the gnu-efi runtime (normally -lgnuefi -lefi).  */
/* ------------------------------------------------------------------ */

/* On x86-64 without the MS ABI, uefi_call_wrapper() routes through the
 * efi_callN() trampolines from libefi. Our mock EFI functions are plain host
 * C functions, so direct casts are ABI-correct here. */
UINT64 efi_call0(void *f)
{
    return ((UINT64 (*)(void))f)();
}

UINT64 efi_call1(void *f, UINT64 a0)
{
    return ((UINT64 (*)(UINT64))f)(a0);
}

UINT64 efi_call2(void *f, UINT64 a0, UINT64 a1)
{
    return ((UINT64 (*)(UINT64, UINT64))f)(a0, a1);
}

UINT64 efi_call3(void *f, UINT64 a0, UINT64 a1, UINT64 a2)
{
    return ((UINT64 (*)(UINT64, UINT64, UINT64))f)(a0, a1, a2);
}

UINT64 efi_call4(void *f, UINT64 a0, UINT64 a1, UINT64 a2, UINT64 a3)
{
    return ((UINT64 (*)(UINT64, UINT64, UINT64, UINT64))f)(a0, a1, a2, a3);
}

UINT64 efi_call5(void *f, UINT64 a0, UINT64 a1, UINT64 a2, UINT64 a3,
        UINT64 a4)
{
    return ((UINT64 (*)(UINT64, UINT64, UINT64, UINT64, UINT64))f)(
            a0, a1, a2, a3, a4);
}

UINT64 efi_call6(void *f, UINT64 a0, UINT64 a1, UINT64 a2, UINT64 a3,
        UINT64 a4, UINT64 a5)
{
    return ((UINT64 (*)(UINT64, UINT64, UINT64, UINT64, UINT64, UINT64))f)(
            a0, a1, a2, a3, a4, a5);
}

UINT64 efi_call7(void *f, UINT64 a0, UINT64 a1, UINT64 a2, UINT64 a3,
        UINT64 a4, UINT64 a5, UINT64 a6)
{
    return ((UINT64 (*)(UINT64, UINT64, UINT64, UINT64, UINT64, UINT64,
            UINT64))f)(a0, a1, a2, a3, a4, a5, a6);
}

UINT64 efi_call8(void *f, UINT64 a0, UINT64 a1, UINT64 a2, UINT64 a3,
        UINT64 a4, UINT64 a5, UINT64 a6, UINT64 a7)
{
    return ((UINT64 (*)(UINT64, UINT64, UINT64, UINT64, UINT64, UINT64,
            UINT64, UINT64))f)(a0, a1, a2, a3, a4, a5, a6, a7);
}

UINT64 efi_call9(void *f, UINT64 a0, UINT64 a1, UINT64 a2, UINT64 a3,
        UINT64 a4, UINT64 a5, UINT64 a6, UINT64 a7, UINT64 a8)
{
    return ((UINT64 (*)(UINT64, UINT64, UINT64, UINT64, UINT64, UINT64,
            UINT64, UINT64, UINT64))f)(a0, a1, a2, a3, a4, a5, a6, a7, a8);
}

UINT64 efi_call10(void *f, UINT64 a0, UINT64 a1, UINT64 a2, UINT64 a3,
        UINT64 a4, UINT64 a5, UINT64 a6, UINT64 a7, UINT64 a8, UINT64 a9)
{
    return ((UINT64 (*)(UINT64, UINT64, UINT64, UINT64, UINT64, UINT64,
            UINT64, UINT64, UINT64, UINT64))f)(a0, a1, a2, a3, a4, a5, a6,
            a7, a8, a9);
}

static EFI_BOOT_SERVICES mock_bs;
EFI_BOOT_SERVICES *BS = &mock_bs;

/* Referenced by efi_main via LoadedImageProtocol. */
EFI_GUID gEfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

void InitializeLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    (void)ImageHandle;
    (void)SystemTable;
}

/* The HAL only ever calls FreePool() on the pointer LibFileInfo() returned,
 * which here is a view into a static buffer (see below). */
void FreePool(VOID *p)
{
    (void)p;
}

/* The mock EFI file, driven by the tests. */
static const uint8_t *mock_data;
static UINT64 mock_size;
static UINT64 mock_pos;
static EFI_STATUS mock_read_status = EFI_SUCCESS;
static int mock_close_count;

static EFI_STATUS EFIAPI mock_file_close(EFI_FILE_HANDLE This)
{
    (void)This;
    mock_close_count++;
    return EFI_SUCCESS;
}

static EFI_FILE_PROTOCOL mock_file_proto;

/* Note: EFI_FILE_HANDLE is already a pointer typedef (EFI_FILE_PROTOCOL *),
 * so NewHandle is EFI_FILE_HANDLE *. */
static EFI_STATUS EFIAPI mock_file_open(EFI_FILE_HANDLE This,
        EFI_FILE_HANDLE *NewHandle, CHAR16 *FileName,
        UINT64 OpenMode, UINT64 Attributes)
{
    (void)This;
    (void)FileName;
    (void)OpenMode;
    (void)Attributes;
    *NewHandle = &mock_file_proto;
    return EFI_SUCCESS;
}

/* Follows the UEFI spec for EFI_FILE_PROTOCOL.Read: *BufferSize receives
 * the number of bytes actually read. On x86-64 that is an eight-byte store,
 * which is exactly what the pre-fix code allowed through its four-byte
 * uint32_t object. */
static EFI_STATUS EFIAPI mock_file_read(EFI_FILE_HANDLE This,
        UINTN *BufferSize, VOID *Buffer)
{
    UINTN wanted;
    UINTN avail;
    UINTN n;

    (void)This;
    if (Buffer == NULL)
        return EFI_INVALID_PARAMETER;
    if (mock_read_status != EFI_SUCCESS)
        return mock_read_status;
    wanted = *BufferSize;
    if (wanted == 0)
        return EFI_INVALID_PARAMETER;
    avail = (UINTN)(mock_size - mock_pos);
    n = (wanted < avail) ? wanted : avail;
    if (n > 0) {
        /* No backing data (e.g. the oversized-file case, where a broken
         * caller tries to read anyway): fail instead of dereferencing
         * NULL. */
        if (mock_data == NULL)
            return EFI_INVALID_PARAMETER;
        memcpy(Buffer, mock_data + mock_pos, n);
    }
    mock_pos += n;
    *BufferSize = n;
    return (n == wanted) ? EFI_SUCCESS : EFI_END_OF_FILE;
}

static const EFI_GUID mock_file_info_id = EFI_FILE_INFO_ID;

/* Spec'd GetInfo layout: the information-type GUID first, then the info
 * structure. */
static EFI_STATUS EFIAPI mock_file_get_info(EFI_FILE_HANDLE This,
        EFI_GUID *InformationType, UINTN *BufferSize, VOID *Buffer)
{
    UINTN need = (UINTN)(sizeof(EFI_GUID) + SIZE_OF_EFI_FILE_INFO);
    EFI_FILE_INFO *info;

    (void)This;
    if (InformationType == NULL ||
            memcmp(InformationType, &mock_file_info_id,
                sizeof(EFI_GUID)) != 0)
        return EFI_UNSUPPORTED;
    if (Buffer == NULL) {
        *BufferSize = need;
        return EFI_SUCCESS;
    }
    if (*BufferSize < need) {
        *BufferSize = need;
        return EFI_BUFFER_TOO_SMALL;
    }
    memcpy(Buffer, &mock_file_info_id, sizeof(EFI_GUID));
    info = (EFI_FILE_INFO *)((uint8_t *)Buffer + sizeof(EFI_GUID));
    memset(info, 0, SIZE_OF_EFI_FILE_INFO);
    info->Size = SIZE_OF_EFI_FILE_INFO;
    info->FileSize = mock_size;
    info->PhysicalSize = mock_size;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_file_dummy(EFI_FILE_HANDLE This)
{
    (void)This;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_file_write(EFI_FILE_HANDLE This,
        UINTN *BufferSize, VOID *Buffer)
{
    (void)This;
    (void)BufferSize;
    (void)Buffer;
    return EFI_WRITE_PROTECTED;
}

static EFI_STATUS EFIAPI mock_file_get_position(EFI_FILE_HANDLE This,
        UINT64 *Position)
{
    (void)This;
    *Position = mock_pos;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_file_set_position(EFI_FILE_HANDLE This,
        UINT64 Position)
{
    (void)This;
    mock_pos = Position;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_file_set_info(EFI_FILE_HANDLE This,
        EFI_GUID *InformationType, UINTN BufferSize, VOID *Buffer)
{
    (void)This;
    (void)InformationType;
    (void)BufferSize;
    (void)Buffer;
    return EFI_WRITE_PROTECTED;
}

static uint8_t mock_info_buf[128];

/* Mirrors gnu-efi LibFileInfo(): a GetInfo() query for EFI_FILE_INFO_ID.
 * The spec'd buffer starts with the information-type GUID, so the returned
 * pointer skips it. The HAL reads ->FileSize and passes the result to
 * FreePool(). */
EFI_FILE_INFO *LibFileInfo(EFI_FILE_HANDLE FHand)
{
    EFI_STATUS status;
    UINTN len = sizeof(mock_info_buf);

    if (FHand == NULL)
        return NULL;
    status = uefi_call_wrapper(FHand->GetInfo, 4, FHand,
            &mock_file_info_id, &len, mock_info_buf);
    if (EFI_ERROR(status))
        return NULL;
    return (EFI_FILE_INFO *)(mock_info_buf + sizeof(EFI_GUID));
}

static int mock_alloc_fail;
static EFI_STATUS EFIAPI mock_allocate_pages(EFI_ALLOCATE_TYPE AllocateType,
        EFI_MEMORY_TYPE MemoryType, UINTN Pages,
        EFI_PHYSICAL_ADDRESS *Memory)
{
    void *p;

    (void)AllocateType;
    (void)MemoryType;
    if (mock_alloc_fail)
        return EFI_OUT_OF_RESOURCES;
    /* Cap so a broken caller cannot exhaust the host. */
    if (Pages == 0 || Pages > 0x10000)
        return EFI_INVALID_PARAMETER;
    p = calloc(Pages, 0x1000);
    if (p == NULL)
        return EFI_OUT_OF_RESOURCES;
    *Memory = (EFI_PHYSICAL_ADDRESS)(uintptr_t)p;
    return EFI_SUCCESS;
}

static int mock_free_pages;
static EFI_STATUS EFIAPI mock_free_pages_fn(EFI_PHYSICAL_ADDRESS Memory,
        UINTN Pages)
{
    (void)Memory;
    (void)Pages;
    mock_free_pages++;
    return EFI_SUCCESS;
}

/* --- x86_64_efi_do_boot() mocks -------------------------------------- */

static EFI_SYSTEM_TABLE mock_st;
static EFI_HANDLE mock_image_handle;
static int mock_load_image_calls;
static int mock_start_image_calls;
static MEMMAP_DEVICE_PATH captured_dp[2];
static EFI_PHYSICAL_ADDRESS captured_src_addr;
static UINTN captured_src_size;

/* Captures the memory device path handed to LoadImage so the tests can
 * check its address range. */
static EFI_STATUS EFIAPI mock_load_image(BOOLEAN BootPolicy,
        EFI_HANDLE ParentImageHandle, EFI_DEVICE_PATH_PROTOCOL *DevicePath,
        VOID *SourceBuffer, UINTN SourceSize, EFI_HANDLE *ImageHandle)
{
    (void)BootPolicy;
    (void)ParentImageHandle;
    if (DevicePath != NULL) {
        memcpy(captured_dp, DevicePath, sizeof(captured_dp));
        captured_src_addr = (EFI_PHYSICAL_ADDRESS)(uintptr_t)SourceBuffer;
        captured_src_size = SourceSize;
    }
    if (ImageHandle != NULL)
        *ImageHandle = (EFI_HANDLE)0xBEEF;
    mock_load_image_calls++;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_start_image(EFI_HANDLE ImageHandle,
        UINTN *ExitDataSize, CHAR16 **ExitData)
{
    (void)ImageHandle;
    (void)ExitDataSize;
    (void)ExitData;
    mock_start_image_calls++;
    return EFI_SUCCESS;
}

/* wolfBoot symbols referenced by the HAL file, not exercised by the tests. */
int wolfBoot_printf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

void wolfBoot_start(void)
{
}

/* Referenced by wolfBoot_efi_get_cmdline() (static inline in
 * wolfboot_efi.h), which only x86_64_efi_do_boot() calls; never reached by
 * the tests. */
uint16_t wolfBoot_find_header(uint8_t *haystack, uint16_t type, uint8_t **ptr)
{
    (void)haystack;
    (void)type;
    (void)ptr;
    return 0;
}

/* Pull in the code under test (its statics become visible here). */
#include "../../hal/x86_64_efi.c"

/* The tests pass their own CHAR16 filename (the mock ignores the content).
 * The build rule uses -fshort-wchar like the real x86_64_efi build, so
 * efi_main's L"..." literals are valid 16-bit CHAR16 strings here too. */
static const CHAR16 test_image_name[] = { 'k', 'e', 'r', 'n', 'e', 'l',
    '.', 'i', 'm', 'g', 0 };

/* ------------------------------------------------------------------ */
/* Test plumbing.                                                      */
/* ------------------------------------------------------------------ */

/* A uint32_t flanked by canary bytes: the pre-fix 8-byte UINTN store lands
 * exactly on canary_after[0..3]. */
typedef struct {
    uint8_t canary_before[8];
    uint32_t sz;
    uint8_t canary_after[8];
} sz_canary_t;

static void canary_reset(sz_canary_t *c)
{
    memset(c, 0, sizeof(*c));
    memset(c->canary_before, 0xA5, sizeof(c->canary_before));
    memset(c->canary_after, 0x5A, sizeof(c->canary_after));
    c->sz = 0xDEADBEEF; /* sentinel: the HAL overwrites it */
}

static int canary_intact(const sz_canary_t *c)
{
    uint8_t b[8];
    uint8_t a[8];

    memset(b, 0xA5, sizeof(b));
    memset(a, 0x5A, sizeof(a));
    return memcmp(c->canary_before, b, sizeof(b)) == 0 &&
           memcmp(c->canary_after, a, sizeof(a)) == 0;
}

static void setup(void)
{
    memset(&mock_bs, 0, sizeof(mock_bs));
    mock_bs.AllocatePages = mock_allocate_pages;
    mock_bs.FreePages = mock_free_pages_fn;
    mock_bs.LoadImage = mock_load_image;
    mock_bs.StartImage = mock_start_image;
    mock_alloc_fail = 0;
    mock_free_pages = 0;
    mock_close_count = 0;
    mock_read_status = EFI_SUCCESS;
    mock_load_image_calls = 0;
    mock_start_image_calls = 0;
    wolfBoot_panicked = 0;

    /* x86_64_efi_do_boot() reads these statics; efi_main() would set them
     * on target. */
    mock_st.BootServices = &mock_bs;
    gSystemTable = &mock_st;
    gImageHandle = &mock_image_handle;

    memset(&mock_file_proto, 0, sizeof(mock_file_proto));
    mock_file_proto.Revision = 0x00120000;
    mock_file_proto.Open = mock_file_open;
    mock_file_proto.Close = mock_file_close;
    mock_file_proto.Delete = mock_file_dummy;
    mock_file_proto.Read = mock_file_read;
    mock_file_proto.Write = mock_file_write;
    mock_file_proto.GetPosition = mock_file_get_position;
    mock_file_proto.SetPosition = mock_file_set_position;
    mock_file_proto.GetInfo = mock_file_get_info;
    mock_file_proto.SetInfo = mock_file_set_info;
    mock_file_proto.Flush = mock_file_dummy;
}

static void teardown(void)
{
}

/* A successful load: return 0, *sz is the completed byte count, the image
 * lands at the allocated address, and nothing past the 4-byte *sz is
 * touched. Before the fix the mock Read()'s 8-byte store zeroed
 * canary_after[0..3] on every run. */
START_TEST(test_open_image_no_clobber)
{
    sz_canary_t canary;
    EFI_PHYSICAL_ADDRESS addr;
    uint8_t data[3 * 0x1000 + 77];
    int i;
    int ret;

    for (i = 0; i < (int)sizeof(data); i++)
        data[i] = (uint8_t)(i & 0xFF);

    mock_data = data;
    mock_size = sizeof(data);
    mock_pos = 0;
    addr = 0;
    canary_reset(&canary);

    ret = open_kernel_image(&mock_file_proto, (CHAR16 *)test_image_name,
                            &addr, &canary.sz);
    ck_assert_int_eq(ret, 0);
    ck_assert(canary_intact(&canary));
    ck_assert_uint_eq(canary.sz, (uint32_t)sizeof(data));
    ck_assert(addr != 0);
    ck_assert_int_eq(memcmp((const void *)(uintptr_t)addr, data,
                            sizeof(data)), 0);
    ck_assert_int_eq(mock_free_pages, 0);
    ck_assert_int_eq(mock_close_count, 1);
}
END_TEST

/* A file shorter than the image header is rejected, and the rejection path
 * still must not clobber the canaries. */
START_TEST(test_open_image_too_small)
{
    sz_canary_t canary;
    EFI_PHYSICAL_ADDRESS addr;
    uint8_t data[IMAGE_HEADER_SIZE - 1];
    int i;
    int ret;

    for (i = 0; i < (int)sizeof(data); i++)
        data[i] = (uint8_t)(0xC0 + (i & 0x0F));

    mock_data = data;
    mock_size = sizeof(data);
    mock_pos = 0;
    addr = 0;
    canary_reset(&canary);

    ret = open_kernel_image(&mock_file_proto, (CHAR16 *)test_image_name,
                            &addr, &canary.sz);
    ck_assert_int_eq(ret, -1);
    ck_assert(canary_intact(&canary));

    /* a rejected load must not publish the allocated address, and
     * the pages plus the file handle must be released. */
    ck_assert_uint_eq(addr, 0);
    ck_assert_int_eq(mock_free_pages, 1);
    ck_assert_int_eq(mock_close_count, 1);
}
END_TEST

/* A file larger than 4 GiB must be rejected before any allocation: the
 * loader's size type is uint32_t, and a 64-bit FileSize() truncated into
 * it would allocate and read a tiny fragment of the real image. The
 * guard runs before AllocatePages, so nothing is allocated and nothing
 * must be freed. */
START_TEST(test_open_image_oversized_rejected)
{
    sz_canary_t canary;
    EFI_PHYSICAL_ADDRESS addr;
    int ret;

    mock_data = NULL;
    mock_size = 0x100000000ULL + 0x1000; /* 4 GiB + 4 KiB */
    mock_pos = 0;
    addr = 0;
    canary_reset(&canary);

    ret = open_kernel_image(&mock_file_proto, (CHAR16 *)test_image_name,
                            &addr, &canary.sz);
    ck_assert_int_eq(ret, -1);
    ck_assert(canary_intact(&canary));
    ck_assert_uint_eq(addr, 0);
    ck_assert_int_eq(mock_free_pages, 0);
    ck_assert_int_eq(mock_close_count, 1);
}
END_TEST

/* A failed Read() must not publish the allocated address, and must free
 * the pages and close the file. */
START_TEST(test_open_image_read_failure)
{
    sz_canary_t canary;
    EFI_PHYSICAL_ADDRESS addr;
    uint8_t data[2 * 0x1000];
    int ret;

    memset(data, 0x33, sizeof(data));
    mock_data = data;
    mock_size = sizeof(data);
    mock_pos = 0;
    mock_read_status = EFI_DEVICE_ERROR;
    addr = 0;
    canary_reset(&canary);

    ret = open_kernel_image(&mock_file_proto, (CHAR16 *)test_image_name,
                            &addr, &canary.sz);
    ck_assert_int_eq(ret, -1);
    ck_assert(canary_intact(&canary));
    ck_assert_uint_eq(addr, 0);
    ck_assert_int_eq(mock_free_pages, 1);
    ck_assert_int_eq(mock_close_count, 1);
}
END_TEST

/* A failed allocation must not publish an address, and must close the
 * file. */
START_TEST(test_open_image_alloc_failure)
{
    sz_canary_t canary;
    EFI_PHYSICAL_ADDRESS addr;
    uint8_t data[2 * 0x1000];
    int ret;

    memset(data, 0x44, sizeof(data));
    mock_data = data;
    mock_size = sizeof(data);
    mock_pos = 0;
    mock_alloc_fail = 1;
    addr = 0;
    canary_reset(&canary);

    ret = open_kernel_image(&mock_file_proto, (CHAR16 *)test_image_name,
                            &addr, &canary.sz);
    ck_assert_int_eq(ret, -1);
    ck_assert(canary_intact(&canary));
    ck_assert_uint_eq(addr, 0);
    ck_assert_int_eq(mock_free_pages, 0);
    ck_assert_int_eq(mock_close_count, 1);
}
END_TEST

/* Exactly IMAGE_HEADER_SIZE is accepted: the completed byte count copied
 * back into *sz must not lose the boundary. */
START_TEST(test_open_image_header_boundary)
{
    sz_canary_t canary;
    EFI_PHYSICAL_ADDRESS addr;
    uint8_t data[IMAGE_HEADER_SIZE];
    int ret;

    memset(data, 0x11, sizeof(data));

    mock_data = data;
    mock_size = sizeof(data);
    mock_pos = 0;
    addr = 0;
    canary_reset(&canary);

    ret = open_kernel_image(&mock_file_proto, (CHAR16 *)test_image_name,
                            &addr, &canary.sz);
    ck_assert_int_eq(ret, 0);
    ck_assert_uint_eq(canary.sz, (uint32_t)IMAGE_HEADER_SIZE);
    ck_assert(canary_intact(&canary));
}
END_TEST

/* The memory device path must describe exactly the image bytes: the
 * UEFI MEMMAP_DEVICE_PATH EndingAddress is inclusive (last valid byte),
 * so it is boot_addr + size - 1, and the path must end with an end node. */
START_TEST(test_do_boot_mem_path_end_inclusive)
{
    uint32_t fw_size = 1024;
    uint32_t *boot_addr;
    uint8_t image[IMAGE_HEADER_SIZE + 1024];
    int i;

    memset(image, 0, sizeof(image));
    memcpy(image, "WOLF", 4);
    memcpy(image + 4, &fw_size, sizeof(fw_size));
    for (i = 0; i < 1024; i++)
        image[IMAGE_HEADER_SIZE + i] = (uint8_t)(i & 0xFF);
    boot_addr = (uint32_t *)(image + IMAGE_HEADER_SIZE);

    x86_64_efi_do_boot(boot_addr, NULL);

    ck_assert_int_eq(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_load_image_calls, 1);
    ck_assert_int_eq(mock_start_image_calls, 1);
    ck_assert_uint_eq(captured_src_addr,
        (EFI_PHYSICAL_ADDRESS)(uintptr_t)boot_addr);
    ck_assert_uint_eq(captured_src_size, fw_size);
    ck_assert_uint_eq(captured_dp[0].Header.Type,
        EFI_DEVICE_PATH_PROTOCOL_HW_TYPE);
    ck_assert_uint_eq(captured_dp[0].Header.SubType,
        EFI_DEVICE_PATH_PROTOCOL_MEM_SUBTYPE);
    ck_assert_uint_eq(captured_dp[0].StartingAddress,
        (EFI_PHYSICAL_ADDRESS)(uintptr_t)boot_addr);
    ck_assert_uint_eq(captured_dp[0].EndingAddress,
        (EFI_PHYSICAL_ADDRESS)((uintptr_t)boot_addr + fw_size - 1));
    /* the path must end with an end node */
    ck_assert_uint_eq(captured_dp[1].Header.Type, END_DEVICE_PATH_TYPE);
    ck_assert_uint_eq(captured_dp[1].Header.SubType,
        END_ENTIRE_DEVICE_PATH_SUBTYPE);
}
END_TEST

/* A zero-size image must be rejected before LoadImage: the inclusive end
 * address would underflow and an empty range would be loaded. */
START_TEST(test_do_boot_zero_size_panics)
{
    uint32_t fw_size = 0;
    uint32_t *boot_addr;
    uint8_t image[IMAGE_HEADER_SIZE + 16];

    memset(image, 0, sizeof(image));
    memcpy(image, "WOLF", 4);
    memcpy(image + 4, &fw_size, sizeof(fw_size));
    boot_addr = (uint32_t *)(image + IMAGE_HEADER_SIZE);

    x86_64_efi_do_boot(boot_addr, NULL);

    ck_assert_int_gt(wolfBoot_panicked, 0);
    ck_assert_int_eq(mock_load_image_calls, 0);
    ck_assert_int_eq(mock_start_image_calls, 0);
}
END_TEST

Suite *efi_x86_open_image_suite(void)
{
    Suite *s = suite_create("efi-x86-open-image");
    TCase *tc = tcase_create("efi-x86-open-image");

    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_open_image_no_clobber);
    tcase_add_test(tc, test_open_image_too_small);
    tcase_add_test(tc, test_open_image_oversized_rejected);
    tcase_add_test(tc, test_open_image_read_failure);
    tcase_add_test(tc, test_open_image_alloc_failure);
    tcase_add_test(tc, test_open_image_header_boundary);
    tcase_add_test(tc, test_do_boot_mem_path_end_inclusive);
    tcase_add_test(tc, test_do_boot_zero_size_panics);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    int fails;
    Suite *s = efi_x86_open_image_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    fails = srunner_ntests_failed(sr);
    srunner_free(sr);

    return fails;
}

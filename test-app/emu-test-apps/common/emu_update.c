#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "emu_app.h"
#include "target.h"
#include "hal.h"
#include "wolfboot/wolfboot.h"

#define MSGSIZE 16
#define PAGESIZE 256

volatile uint32_t systick_ms = 0;

static uint8_t page[PAGESIZE];
static uint8_t msg[MSGSIZE];

static const uint8_t ERR = '!';
static const uint8_t START = '*';
static const uint8_t ACK = '#';

static uint32_t emu_im2n(uint32_t val)
{
#ifdef BIG_ENDIAN_ORDER
    val = (((val & 0x000000FFu) << 24) |
           ((val & 0x0000FF00u) << 8) |
           ((val & 0x00FF0000u) >> 8) |
           ((val & 0xFF000000u) >> 24));
#endif
    return val;
}

static uint8_t emu_read_u8(uintptr_t addr)
{
    uint8_t v;
    __asm volatile("ldrb %0, [%1]" : "=r"(v) : "r"(addr) : "memory");
    return v;
}

static uint32_t emu_read_u32(uintptr_t addr)
{
    uint32_t v;
    __asm volatile("ldr %0, [%1]" : "=r"(v) : "r"(addr) : "memory");
    return v;
}

static uint32_t emu_get_blob_version_addr(uintptr_t base)
{
    uintptr_t p = base + IMAGE_HEADER_OFFSET;
    uintptr_t max_p = base + IMAGE_HEADER_SIZE;
    uint32_t magic = emu_read_u32(base);

    if (magic != WOLFBOOT_MAGIC) {
        return 0;
    }

    while ((p + 4u) <= max_p) {
        uint16_t htype = (uint16_t)(emu_read_u8(p) | (emu_read_u8(p + 1u) << 8));
        uint16_t len;

        if (htype == 0u) {
            break;
        }
        if ((emu_read_u8(p) == HDR_PADDING) || ((p & 1u) != 0u)) {
            p++;
            continue;
        }

        len = (uint16_t)(emu_read_u8(p + 2u) | (emu_read_u8(p + 3u) << 8));
        if ((4u + len) > (uint16_t)(IMAGE_HEADER_SIZE - IMAGE_HEADER_OFFSET)) {
            break;
        }
        if (p + 4u + len > max_p) {
            break;
        }

        p += 4u;
        if (htype == HDR_VERSION) {
            return emu_im2n(emu_read_u32(p));
        }
        p += len;
    }
    return 0;
}

static uint32_t emu_current_version(void)
{
    uintptr_t addr = (uintptr_t)WOLFBOOT_PARTITION_BOOT_ADDRESS;

#ifdef WOLFCRYPT_SECURE_MODE
    return wolfBoot_nsc_current_firmware_version();
#else
    if (addr == 0u) {
        return emu_get_blob_version_addr(0u);
    }
    return wolfBoot_get_blob_version((uint8_t *)addr);
#endif
}

void emu_uart_putc(char c)
{
    emu_uart_write((uint8_t)c);
}

static void uart_write_buf(const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; ++i) {
        emu_uart_write(buf[i]);
    }
}

static uint8_t uart_read_blocking(void)
{
    uint8_t c = 0;
    while (!emu_uart_read(&c)) {
        __asm volatile("nop");
    }
    return c;
}

static void ack(uint32_t off)
{
    uint8_t *bytes = (uint8_t *)&off;
    uint32_t i;
    emu_uart_write(ACK);
    for (i = 0; i < 4; i++) {
        emu_uart_write(bytes[i]);
    }
}

static int check(uint8_t *pkt, int size)
{
    int i;
    uint16_t c = 0;
    uint16_t c_rx = *((uint16_t *)(pkt + 2));
    uint16_t *p = (uint16_t *)(pkt + 4);
    for (i = 0; i < ((size - 4) >> 1); i++) {
        c += p[i];
    }
    if (c == c_rx) {
        return 0;
    }
    return -1;
}

static void wait_for_update(uint32_t version)
{
    uint32_t tlen = 0;
    uint32_t recv_seq = 0;
    uint32_t r_total = 0;
    uint32_t tot_len = 0;
    uint32_t next_seq = 0;
    uint8_t *v_array = (uint8_t *)&version;
    int i;

    memset(page, 0xFF, PAGESIZE);

#ifndef WOLFCRYPT_SECURE_MODE
    hal_flash_unlock();
#endif

    emu_uart_write(START);
    for (i = 3; i >= 0; i--) {
        emu_uart_write(v_array[i]);
    }

    while (1) {
        r_total = 0;
        do {
            while (r_total < 2) {
                msg[r_total++] = uart_read_blocking();
                if ((r_total == 2) && ((msg[0] != 0xA5) || (msg[1] != 0x5A))) {
                    r_total = 0;
                    continue;
                }
            }
            msg[r_total++] = uart_read_blocking();
            if ((tot_len == 0) && r_total == 2 + sizeof(uint32_t)) {
                break;
            }
            if ((r_total > 8) && (tot_len <= ((r_total - 8) + next_seq))) {
                break;
            }
        } while (r_total < MSGSIZE);

        if (tot_len == 0)  {
            tlen = msg[2] + (msg[3] << 8) + (msg[4] << 16) + (msg[5] << 24);
            if (tlen > WOLFBOOT_PARTITION_SIZE - 8) {
                emu_uart_write(ERR);
                emu_uart_write(ERR);
                emu_uart_write(ERR);
                emu_uart_write(ERR);
                emu_uart_write(START);
                recv_seq = 0;
                tot_len = 0;
                continue;
            }
            tot_len = tlen;
            ack(0);
            continue;
        }

        if (check(msg, r_total) < 0) {
            ack(next_seq);
            continue;
        }

        recv_seq = msg[4] + (msg[5] << 8) + (msg[6] << 16) + (msg[7] << 24);
        if (recv_seq == next_seq) {
            int psize = r_total - 8;
            int page_idx = (int)(recv_seq % PAGESIZE);
            memcpy(&page[page_idx], msg + 8, psize);
            page_idx += psize;
            if ((page_idx == PAGESIZE) || (next_seq + (uint32_t)psize >= tot_len)) {
                uint32_t dst = (WOLFBOOT_PARTITION_UPDATE_ADDRESS + recv_seq + (uint32_t)psize) - (uint32_t)page_idx;
                uint32_t dst_off = (recv_seq + (uint32_t)psize) - (uint32_t)page_idx;
#ifdef WOLFCRYPT_SECURE_MODE
                if ((dst_off % WOLFBOOT_SECTOR_SIZE) == 0u) {
                    wolfBoot_nsc_erase_update(dst_off, WOLFBOOT_SECTOR_SIZE);
                }
                wolfBoot_nsc_write_update(dst_off, page, PAGESIZE);
#else
                if ((dst % WOLFBOOT_SECTOR_SIZE) == 0u) {
                    hal_flash_erase(dst, WOLFBOOT_SECTOR_SIZE);
                }
                hal_flash_write(dst, page, PAGESIZE);
#endif
                memset(page, 0xFF, PAGESIZE);
            }
            next_seq += (uint32_t)psize;
        }
        ack(next_seq);
        if (next_seq >= tot_len) {
            uint32_t update_ver;
#ifdef WOLFCRYPT_SECURE_MODE
            update_ver = wolfBoot_nsc_update_firmware_version();
#else
            update_ver = wolfBoot_get_blob_version((uint8_t *)WOLFBOOT_PARTITION_UPDATE_ADDRESS);
#endif
            if (update_ver == 7u) {
                __asm volatile("bkpt #0x4D");
                break;
            }
#ifdef WOLFCRYPT_SECURE_MODE
            wolfBoot_nsc_update_trigger();
#else
            wolfBoot_update_trigger();
#endif
            __asm volatile("bkpt #0x47");
            break;
        }
    }

#ifndef WOLFCRYPT_SECURE_MODE
    hal_flash_lock();
#endif

    while (1) {
        __asm volatile("wfi");
    }
}

int main(void)
{
    uint32_t version;

    emu_uart_init();

    version = emu_current_version();
    printf("get_version=%lu\n", (unsigned long)version);

    if (version == 4u) {
#ifdef WOLFCRYPT_SECURE_MODE
        wolfBoot_nsc_success();
#else
        wolfBoot_success();
#endif
        __asm volatile("bkpt #0x4A");
        while (1) {
            __asm volatile("wfi");
        }
    }
    if (version == 3u) {
        __asm volatile("bkpt #0x4B");
        while (1) {
            __asm volatile("wfi");
        }
    }
    if (version == 8u) {
#ifdef WOLFCRYPT_SECURE_MODE
        wolfBoot_nsc_success();
#else
        wolfBoot_success();
#endif
        __asm volatile("bkpt #0x4E");
        while (1) {
            __asm volatile("wfi");
        }
    }

    wait_for_update(version);
    return 0;
}

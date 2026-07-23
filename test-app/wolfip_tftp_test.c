/* wolfip_tftp_test.c
 *
 * Optional wolfIP network test for the wolfBoot test-app. Ethernet port and ms
 * timebase are compile-time selected: NXP QorIQ FMan (T2080/T1024/T1040,
 * big-endian PowerPC) or NXP Layerscape ENETC (LS1028A, AArch64). Modes:
 *   default              DHCP lease only
 *   WOLFBOOT_TEST_TFTP   + TFTP RRQ fetch into RAM, report size+checksum
 *                          (optionally verify them via TFTP_EXPECT_SIZE/SUM)
 *   WOLFIP_SPEED_TEST    + TCP throughput server on port 9 (benchmark)
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

#include <stdint.h>
#include <stddef.h>

#include "printf.h"
#include "wolfip.h"
#ifdef WOLFBOOT_TEST_TFTP
#include "src/tftp/wolftftp.h"
#endif

/* Port selection: WOLFIP_PORT_ENETC -> LS1028A ENETC, else QorIQ FMan. Driver
 * entry points are wrapped so the bring-up code below is port-agnostic. */
#if defined(WOLFIP_PORT_ENETC)
#include "nxp_enetc.h"
#define WOLFIP_NETDEV_INIT      nxp_enetc_init
#define WOLFIP_NETDEV_PHY_ADDR  nxp_enetc_phy_addr
#define WOLFIP_NETDEV_PHY_READ  nxp_enetc_phy_read
#define WOLFIP_NETDEV_LINK_UP   nxp_enetc_link_up
#define WOLFIP_NETDEV_SET_LOG   nxp_enetc_set_log
#define WOLFIP_NETDEV_NAME      "ENETC"
#else
#include "nxp_fman.h"
#define WOLFIP_NETDEV_INIT      nxp_fman_init
#define WOLFIP_NETDEV_PHY_ADDR  nxp_fman_phy_addr
#define WOLFIP_NETDEV_PHY_READ  nxp_fman_phy_read
#define WOLFIP_NETDEV_PHY_READ_AT nxp_fman_phy_read_at
#define WOLFIP_NETDEV_LINK_UP   nxp_fman_link_up
#define WOLFIP_NETDEV_NAME      "FMan mEMAC"
#endif

/* Host running in.tftpd / the throughput peer (same subnet as the DHCP lease).
 * Override from the build config, e.g.
 * CFLAGS_EXTRA+=-DHOST_IP_STR='"192.168.1.5"' -DTFTP_FILENAME='"img.bin"'. */
#ifndef HOST_IP_STR
#define HOST_IP_STR     "10.0.4.24"
#endif
#ifndef TFTP_FILENAME
#define TFTP_FILENAME   "wolfip_test.bin"
#endif
/* Optional integrity check: when non-zero, the fetched byte count and/or
 * additive 32-bit checksum must match or the test FAILS; 0 (default) just
 * reports the measured values. E.g. CFLAGS_EXTRA+=-DTFTP_EXPECT_SIZE=8192
 * -DTFTP_EXPECT_SUM=0xFF000. */
#ifndef TFTP_EXPECT_SIZE
#define TFTP_EXPECT_SIZE 0U
#endif
#ifndef TFTP_EXPECT_SUM
#define TFTP_EXPECT_SUM  0U
#endif

/* Millisecond clock from a free-running counter, self-contained (no bootloader
 * symbol). wolfIP timers are coarse so exactness is moot. */
#if defined(__aarch64__)
/* ARMv8 generic timer: CNTPCT_EL0 counts at CNTFRQ_EL0 Hz. */
static uint64_t read_tb(void)
{
    uint64_t v;
    __asm__ __volatile__("isb; mrs %0, cntpct_el0" : "=r"(v));
    return v;
}
static uint64_t tb_hz(void)
{
    static uint64_t cached = 0; /* CNTFRQ is a boot constant; read once */
    uint64_t f;
    if (cached != 0)
        return cached;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(f));
    cached = f ? f : 25000000ULL; /* LS1028A SYSCLK-derived default */
    return cached;
}
static uint64_t now_ms(void)
{
    return read_tb() / (tb_hz() / 1000ULL);
}
#else
/* PowerPC e5500/e6500 Time Base via SPRs (TBL=268, TBU=269) = platform clock
 * / 16; default is the VPX3-152 value (600 MHz -> 37.5 MHz). Other platform
 * clocks MUST override this -- it scales the wolfIP TCP/DHCP timers fed by
 * now_ms, not just harness timeouts -- e.g. -DTIMEBASE_HZ=50000000ULL. */
#ifndef TIMEBASE_HZ
#define TIMEBASE_HZ     37500000ULL
#endif

static uint64_t read_tb(void)
{
    uint32_t hi, lo, hi2;
    do {
        __asm__ __volatile__("mfspr %0, 269" : "=r"(hi));
        __asm__ __volatile__("mfspr %0, 268" : "=r"(lo));
        __asm__ __volatile__("mfspr %0, 269" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t now_ms(void)
{
    return (uint64_t)(read_tb() / (TIMEBASE_HZ / 1000ULL));
}
#endif

/* wolfIP needs an RNG (TCP ISN, DHCP xid, ephemeral ports). LFSR seeded
 * from the free-running timebase. NOT cryptographic. */
uint32_t wolfIP_getrandom(void)
{
    static uint32_t lfsr;
    if (lfsr == 0)
        lfsr = (uint32_t)read_tb() | 1U;
    lfsr ^= (uint32_t)read_tb();
    lfsr ^= lfsr << 13;
    lfsr ^= lfsr >> 17;
    lfsr ^= lfsr << 5;
    return lfsr;
}

#define DHCP_TIMEOUT_MS 30000ULL

#if defined(WOLFIP_NETDEV_SET_LOG) && defined(WOLFIP_DRIVER_LOG)
/* Driver bring-up stage logger (opt-in via -DWOLFIP_DRIVER_LOG). */
static void wolfip_driver_log(const char *msg)
{
    wolfBoot_printf("  drv: %s\r\n", msg);
}
#endif

/* Bring up the driver + wolfIP + DHCP. Returns the stack on a bound lease,
 * NULL on failure. Prints PHY/link and the lease. */
static struct wolfIP *wolfip_bringup(void)
{
    struct wolfIP *s = NULL;
    struct wolfIP_ll_dev *ll;
    uint64_t start, now;
    ip4 ip = 0, nm = 0, gw = 0;
    int rc, addr;
    uint16_t id1, bsr;

    wolfIP_init_static(&s);
    ll = wolfIP_getdev(s);

    wolfBoot_printf("wolfIP: bringing up %s\r\n", WOLFIP_NETDEV_NAME);
#if defined(WOLFIP_NETDEV_SET_LOG) && defined(WOLFIP_DRIVER_LOG)
    /* Print each bring-up stage so a hang/fault is localized by the last
     * marker (enable with -DWOLFIP_DRIVER_LOG). */
    WOLFIP_NETDEV_SET_LOG(wolfip_driver_log);
#endif
    rc = WOLFIP_NETDEV_INIT(ll, NULL);
    if (rc < 0) {
        wolfBoot_printf("wolfIP: netdev init failed rc=%d\r\n", rc);
        return NULL;
    }
    addr = WOLFIP_NETDEV_PHY_ADDR();
    id1 = WOLFIP_NETDEV_PHY_READ(0x02);
    bsr = WOLFIP_NETDEV_PHY_READ(0x01);
    wolfBoot_printf("wolfIP: PHY addr=%d ID1=0x%x BSR=0x%x link=%s\r\n",
        addr, id1, bsr, WOLFIP_NETDEV_LINK_UP() ? "UP" : "down");

#if defined(WOLFIP_PHY_SCAN) && defined(WOLFIP_NETDEV_PHY_READ_AT)
    /* MDIO bus scan: print ID1/ID2/BSR for every clause-22 address to identify
     * the cabled port (BSR bit 2 = link up). Diagnostic; -DWOLFIP_PHY_SCAN. */
    {
        int a;
        uint16_t i1, i2, bs;
        wolfBoot_printf("wolfIP: MDIO scan (addr: ID1 ID2 BSR link)\r\n");
        for (a = 0; a < 32; a++) {
            i1 = WOLFIP_NETDEV_PHY_READ_AT((uint8_t)a, 0x02);
            if (i1 == 0xFFFFU || i1 == 0x0000U)
                continue; /* no PHY at this address */
            i2 = WOLFIP_NETDEV_PHY_READ_AT((uint8_t)a, 0x03);
            bs = WOLFIP_NETDEV_PHY_READ_AT((uint8_t)a, 0x01);
            wolfBoot_printf("  %d: 0x%x 0x%x 0x%x %s\r\n",
                a, i1, i2, bs, (bs & 0x0004U) ? "UP" : "down");
        }
    }
#endif

    (void)wolfIP_poll(s, now_ms());
    wolfBoot_printf("wolfIP: starting DHCP...\r\n");
    (void)dhcp_client_init(s);

    start = now_ms();
    for (;;) {
        now = now_ms();
        (void)wolfIP_poll(s, now);
        if (dhcp_bound(s)) {
            wolfIP_ipconfig_get(s, &ip, &nm, &gw);
            wolfBoot_printf("wolfIP: DHCP bound ip=%u.%u.%u.%u gw=%u.%u.%u.%u\r\n",
                (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
                (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF),
                (unsigned)((gw >> 24) & 0xFF), (unsigned)((gw >> 16) & 0xFF),
                (unsigned)((gw >> 8) & 0xFF), (unsigned)(gw & 0xFF));
            return s;
        }
        if ((now - start) > DHCP_TIMEOUT_MS) {
            wolfBoot_printf("wolfIP: DHCP timed out\r\n");
            return NULL;
        }
    }
}

#ifdef WOLFBOOT_TEST_TFTP
/* ---- TFTP RRQ fetch into a RAM buffer + integrity check ------------- */
#define TFTP_RAM_MAX    (128U * 1024U)
#define TFTP_LOCAL_PORT 6900U

struct tftp_ram_ctx {
    uint8_t  buf[TFTP_RAM_MAX];
    uint32_t len;       /* highest offset+len written */
    uint32_t sum;       /* additive 32-bit checksum of all bytes */
    int      overflow;
};
static struct tftp_ram_ctx g_tftp;

struct tftp_glue { struct wolfIP *s; int sock; };

/* Zero a local struct without pulling in a libc memset (freestanding app). */
static void zmem(void *p, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++)
        ((uint8_t *)p)[i] = 0;
}

static int tftp_io_open(void *arg, const char *name, int is_write,
    uint32_t *size_hint, void **handle)
{
    (void)name; (void)is_write; (void)size_hint;
    g_tftp.len = 0;
    g_tftp.sum = 0;
    g_tftp.overflow = 0;
    *handle = &g_tftp;
    return 0;
}

static int tftp_io_write(void *arg, void *handle, uint32_t offset,
    const uint8_t *buf, uint16_t len)
{
    struct tftp_ram_ctx *c = (struct tftp_ram_ctx *)handle;
    uint16_t i;
    (void)arg;
    /* Overflow-safe bounds check: never compute offset+len (could wrap a
     * 32-bit offset). Compare against the remaining space instead. */
    if (offset > TFTP_RAM_MAX || len > TFTP_RAM_MAX - offset) {
        c->overflow = 1;
        return -1;
    }
    for (i = 0; i < len; i++) {
        c->buf[offset + i] = buf[i];
        c->sum += buf[i];
    }
    if (offset + len > c->len)
        c->len = offset + len;
    return 0;
}

static int tftp_send(void *arg, uint16_t local_port,
    const struct wolftftp_endpoint *remote, const uint8_t *buf, uint16_t len)
{
    struct tftp_glue *g = (struct tftp_glue *)arg;
    struct wolfIP_sockaddr_in dst;
    int ret;
    (void)local_port;
    zmem(&dst, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = ee16(remote->port);
    dst.sin_addr.s_addr = ee32(remote->ip);
    ret = wolfIP_sock_sendto(g->s, g->sock, buf, len, 0,
        (struct wolfIP_sockaddr *)&dst, sizeof(dst));
    return (ret == (int)len) ? 0 : (ret < 0 ? ret : -1);
}

static int run_tftp_fetch(struct wolfIP *s)
{
    struct tftp_glue glue;
    struct wolftftp_transport_ops transport;
    struct wolftftp_io_ops io;
    struct wolftftp_transfer_cfg cfg;
    struct wolftftp_client client;
    struct wolftftp_endpoint srv;
    struct wolfIP_sockaddr_in bind_addr;
    uint8_t pkt[1500];
    int sock, ret = -1;
    uint64_t deadline, now;

    sock = wolfIP_sock_socket(s, AF_INET, IPSTACK_SOCK_DGRAM, 0);
    if (sock < 0) {
        wolfBoot_printf("TFTP: udp socket failed\r\n");
        return -1;
    }
    zmem(&bind_addr, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = ee16(TFTP_LOCAL_PORT);
    if (wolfIP_sock_bind(s, sock, (struct wolfIP_sockaddr *)&bind_addr,
            sizeof(bind_addr)) < 0) {
        wolfBoot_printf("TFTP: bind to local port %u failed\r\n",
            TFTP_LOCAL_PORT);
        goto out;
    }

    glue.s = s; glue.sock = sock;
    zmem(&transport, sizeof(transport));
    transport.send = tftp_send;
    transport.arg = &glue;
    zmem(&io, sizeof(io));
    io.open = tftp_io_open;
    io.write = tftp_io_write;
    io.arg = &g_tftp;
    zmem(&cfg, sizeof(cfg));
    cfg.local_port = TFTP_LOCAL_PORT;
    cfg.blksize = WOLFTFTP_DEFAULT_BLKSIZE;
    cfg.timeout_s = WOLFTFTP_DEFAULT_TIMEOUT_S;
    cfg.windowsize = 1;
    cfg.max_retries = 5;

    wolftftp_client_init(&client, &transport, &io, &cfg);
    zmem(&srv, sizeof(srv));
    srv.ip = atoip4(HOST_IP_STR);
    srv.port = 69;

    wolfBoot_printf("TFTP: RRQ %s from %s\r\n", TFTP_FILENAME, HOST_IP_STR);
    if (wolftftp_client_start_rrq(&client, &srv, TFTP_FILENAME) != 0) {
        wolfBoot_printf("TFTP: start_rrq failed\r\n");
        goto out;
    }

    now = now_ms();
    deadline = now + 20000ULL;
    while (client.state != WOLFTFTP_CLIENT_COMPLETE &&
           client.state != WOLFTFTP_CLIENT_ERROR &&
           now < deadline) {
        struct wolfIP_sockaddr_in rem;
        uint32_t rlen;
        int n;
        (void)wolfIP_poll(s, now);
        for (;;) {
            rlen = sizeof(rem);
            n = wolfIP_sock_recvfrom(s, sock, pkt, sizeof(pkt), 0,
                (struct wolfIP_sockaddr *)&rem, &rlen);
            if (n <= 0)
                break;
            {
                struct wolftftp_endpoint rep;
                rep.ip = ee32(rem.sin_addr.s_addr);
                rep.port = ee16(rem.sin_port);
                (void)wolftftp_client_receive(&client, TFTP_LOCAL_PORT,
                    &rep, pkt, (uint16_t)n);
            }
        }
        (void)wolftftp_client_poll(&client, (uint32_t)now);
        now = now_ms();
    }

    if (client.state != WOLFTFTP_CLIENT_COMPLETE) {
        wolfBoot_printf("TFTP: FAILED (state=%d status=%d)\r\n",
            client.state, client.last_status);
        goto out;
    }
    if (g_tftp.overflow) {
        wolfBoot_printf("TFTP: file exceeds %u byte RAM buffer\r\n", TFTP_RAM_MAX);
        goto out;
    }
    wolfBoot_printf("TFTP: got %u bytes, checksum 0x%x\r\n",
        (unsigned)g_tftp.len, (unsigned)g_tftp.sum);
#if (TFTP_EXPECT_SIZE != 0U)
    if (g_tftp.len != (uint32_t)TFTP_EXPECT_SIZE) {
        wolfBoot_printf("TFTP: size mismatch (expected %u)\r\n",
            (unsigned)TFTP_EXPECT_SIZE);
        goto out;
    }
#endif
#if (TFTP_EXPECT_SUM != 0U)
    if (g_tftp.sum != (uint32_t)TFTP_EXPECT_SUM) {
        wolfBoot_printf("TFTP: checksum mismatch (expected 0x%x)\r\n",
            (unsigned)TFTP_EXPECT_SUM);
        goto out;
    }
#endif
    ret = 0;

out:
    (void)wolfIP_sock_close(s, sock);
    return ret;
}
#endif /* WOLFBOOT_TEST_TFTP */

#ifdef WOLFIP_SPEED_TEST
/* ---- Benchmark: one-connection TCP throughput server on port 9 ------- *
 * RX sinks everything the peer sends; TX pushes a chargen buffer whenever
 * writable. Prints B/s on close.
 *   RX (board sinks):   dd if=/dev/zero bs=1460 count=N | nc <ip> 9
 *   TX (board sources): nc <ip> 9 </dev/null | pv -r >/dev/null            */
#define SPEED_PORT      9
static int      speed_listen_fd = -1;
static int      speed_client_fd = -1;
static uint64_t speed_rx_bytes, speed_tx_bytes, speed_start_ms;
static uint8_t  speed_buf[1460];

static void speed_print(void)
{
    uint64_t ms = now_ms() - speed_start_ms;
    if (ms == 0) ms = 1;
    wolfBoot_printf("SPEED done %ums  RX %u B (~%u B/s)  TX %u B (~%u B/s)\r\n",
        (unsigned)ms, (unsigned)speed_rx_bytes,
        (unsigned)((speed_rx_bytes * 1000ULL) / ms),
        (unsigned)speed_tx_bytes,
        (unsigned)((speed_tx_bytes * 1000ULL) / ms));
}

static void speed_cb(int fd, uint16_t event, void *arg)
{
    struct wolfIP *s = (struct wolfIP *)arg;
    int n, k;

    if (fd == speed_listen_fd) {
        if (event & CB_EVENT_READABLE) {
            int c = wolfIP_sock_accept(s, speed_listen_fd, NULL, NULL);
            if (c >= 0) {
                if (speed_client_fd >= 0) {
                    (void)wolfIP_sock_close(s, c);
                } else {
                    speed_client_fd = c;
                    speed_rx_bytes = 0; speed_tx_bytes = 0;
                    speed_start_ms = now_ms();
                    wolfIP_register_callback(s, c, speed_cb, s);
                    wolfBoot_printf("SPEED client connected\r\n");
                }
            }
        }
        return;
    }
    if (fd != speed_client_fd)
        return;

    if (event & CB_EVENT_READABLE) {
        k = 0;
        do {
            n = wolfIP_sock_recvfrom(s, fd, speed_buf, sizeof(speed_buf), 0,
                NULL, NULL);
            if (n > 0)
                speed_rx_bytes += (uint64_t)n;
        } while (n > 0 && ++k < 32);
    }
    if (event & CB_EVENT_WRITABLE) {
        k = 0;
        do {
            n = wolfIP_sock_send(s, fd, speed_buf, sizeof(speed_buf), 0);
            if (n > 0)
                speed_tx_bytes += (uint64_t)n;
        } while (n > 0 && ++k < 32);
    }
    if (event & CB_EVENT_CLOSED) {
        speed_print();
        (void)wolfIP_sock_close(s, fd);
        speed_client_fd = -1;
    }
}

static int run_speed_server(struct wolfIP *s)
{
    struct wolfIP_sockaddr_in addr;

    speed_listen_fd = wolfIP_sock_socket(s, AF_INET, IPSTACK_SOCK_STREAM, 0);
    if (speed_listen_fd < 0) {
        wolfBoot_printf("SPEED: socket failed\r\n");
        return -1;
    }
    wolfIP_register_callback(s, speed_listen_fd, speed_cb, s);
    zmem(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = ee16(SPEED_PORT);
    if (wolfIP_sock_bind(s, speed_listen_fd,
            (struct wolfIP_sockaddr *)&addr, sizeof(addr)) < 0) {
        wolfBoot_printf("SPEED: bind to port %d failed\r\n", SPEED_PORT);
        (void)wolfIP_sock_close(s, speed_listen_fd);
        speed_listen_fd = -1;
        return -1;
    }
    if (wolfIP_sock_listen(s, speed_listen_fd, 1) < 0) {
        wolfBoot_printf("SPEED: listen on port %d failed\r\n", SPEED_PORT);
        (void)wolfIP_sock_close(s, speed_listen_fd);
        speed_listen_fd = -1;
        return -1;
    }

    wolfBoot_printf("SPEED: TCP throughput server on port %d. Drive from host:\r\n",
        SPEED_PORT);
    wolfBoot_printf("  RX: dd if=/dev/zero bs=1460 count=20000 | nc <ip> %d\r\n",
        SPEED_PORT);
    wolfBoot_printf("  TX: nc <ip> %d </dev/null | pv -r >/dev/null\r\n",
        SPEED_PORT);
    for (;;)
        (void)wolfIP_poll(s, now_ms());
    /* not reached */
}
#endif /* WOLFIP_SPEED_TEST */

int wolfip_tftp_test_run(void)
{
    struct wolfIP *s = wolfip_bringup();
    if (s == NULL)
        return -1;
#if defined(WOLFBOOT_TEST_TFTP)
    return run_tftp_fetch(s);
#elif defined(WOLFIP_SPEED_TEST)
    return run_speed_server(s); /* loops forever */
#else
    return 0; /* DHCP lease only, no transfer */
#endif
}

void wolfip_tftp_test_report(void)
{
    wolfBoot_printf("\r\nStarting wolfIP network test...\r\n");
    if (wolfip_tftp_test_run() == 0)
        wolfBoot_printf("WOLFIP_TEST: PASS\r\n");
    else
        wolfBoot_printf("WOLFIP_TEST: FAIL\r\n");
}

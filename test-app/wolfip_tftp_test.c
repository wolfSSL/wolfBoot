/* wolfip_tftp_test.c
 *
 * Optional wolfIP network test for the wolfBoot test-app. Ethernet port and ms
 * timebase are compile-time selected: NXP QorIQ FMan (T2080/T1024/T1040,
 * big-endian PowerPC), NXP Layerscape ENETC (LS1028A, AArch64) or the
 * NXP MCX E247 ENET (Cortex-M4). Modes:
 *   default              DHCP lease only
 *   WOLFBOOT_TEST_TFTP   + TFTP RRQ fetch into RAM, report size+checksum
 *                          (optionally verify them via TFTP_EXPECT_SIZE/SUM)
 *   WOLFIP_SPEED_TEST    + TCP and UDP throughput server on port 9
 *                          (benchmark, host side tools/scripts/wolfip_speed.py)
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
#include "wolfip_tftp_test.h"
#ifdef WOLFBOOT_TEST_TFTP
#include "src/tftp/wolftftp.h"
#endif

/* Port selection:
 *   WOLFIP_PORT_ENETC -> LS1028A ENETC
 *   WOLFIP_PORT_MCXE_ENET -> MCXE247 ENET
 *   else QorIQ FMan
 * Driver entry points are wrapped so the bring-up code below is port-agnostic. */
#if defined(WOLFIP_PORT_ENETC)
#include "nxp_enetc.h"
#define WOLFIP_NETDEV_INIT      nxp_enetc_init
#define WOLFIP_NETDEV_PHY_ADDR  nxp_enetc_phy_addr
#define WOLFIP_NETDEV_PHY_READ  nxp_enetc_phy_read
#define WOLFIP_NETDEV_LINK_UP   nxp_enetc_link_up
#define WOLFIP_NETDEV_SET_LOG   nxp_enetc_set_log
#define WOLFIP_NETDEV_NAME      "ENETC"
#elif defined(WOLFIP_PORT_MCXE_ENET)
#include "nxp_mcxe_enet.h"
#define WOLFIP_NETDEV_INIT      nxp_mcxe_enet_init
#define WOLFIP_NETDEV_PHY_ADDR  nxp_mcxe_enet_phy_addr
#define WOLFIP_NETDEV_PHY_READ  nxp_mcxe_enet_phy_read
#define WOLFIP_NETDEV_LINK_UP   nxp_mcxe_enet_link_up
#define WOLFIP_NETDEV_NAME      "MCXE247 ENET"
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
#elif defined(WOLFIP_PORT_MCXE_ENET)
/* 1 ms SysTick count, kept by the target app. */
#define SYST_CVR_REG    (*(volatile uint32_t *)0xE000E018UL)

/* Combine the tick count and the SysTick down-counter to seed the RNG. */
static uint64_t read_tb(void)
{
    return ((uint64_t)wolfip_test_get_ms() << 24) | (SYST_CVR_REG & 0xFFFFFFU);
}

static uint64_t now_ms(void)
{
    return wolfip_test_get_ms();
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
        wolfBoot_printf("wolfIP: netdev init failed rc=%d, link is %s\r\n",
            rc, WOLFIP_NETDEV_LINK_UP() ? "UP" : "down");
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

#if defined(WOLFBOOT_TEST_TFTP) || defined(WOLFIP_SPEED_TEST)
/* Zero a local struct without pulling in a libc memset (freestanding app). */
static void zmem(void *p, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++)
        ((uint8_t *)p)[i] = 0;
}
#endif

#ifdef WOLFBOOT_TEST_TFTP
/* ---- TFTP RRQ fetch into a RAM buffer + integrity check ------------- */
#ifndef TFTP_RAM_MAX
#define TFTP_RAM_MAX    (128U * 1024U)
#endif
#define TFTP_LOCAL_PORT 6900U

struct tftp_ram_ctx {
    uint8_t  buf[TFTP_RAM_MAX];
    uint32_t len;       /* highest offset+len written */
    uint32_t sum;       /* additive 32-bit checksum of all bytes */
    int      overflow;
};
static struct tftp_ram_ctx g_tftp;

struct tftp_glue { struct wolfIP *s; int sock; };

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
/* ---- Benchmark: TCP and UDP throughput server on port 9 ------------- *
 * Driven from a host by tools/scripts/wolfip_speed.py. Directions are
 * named from the board: rx = the board receives, tx = the board sends,
 * bidir = both at once.
 *
 * TCP: the first byte the client sends picks the direction: 'r' rx,
 * 't' tx, anything else bidir. A client that sends nothing for
 * SPEED_TCP_MODE_MS gets bidir, so plain "nc <ip> 9" works whether or not
 * it sends. The test ends when the client closes. TCP clients are refused
 * while a UDP test runs.
 *
 * UDP: each datagram starts with a SPEED_HDR_LEN byte header, big-endian:
 *   [0] SPEED_MAGIC  [1] command  [2..3] board datagram size
 *   [4..7] sequence  [8..11] board send duration in ms
 *   [12..15] session: a non-zero id the host picks for each test. Data of
 *            a test that has ended is ignored, so datagrams still queued
 *            on the way cannot start a new test.
 * Commands to the board:
 *   'r'  data to count (starts an rx test)
 *   'b'  data to count; the first one starts a bidir test and the board
 *        sends to the client for the given duration, which also ends
 *        the test
 *   't'  start a tx test: the board sends to the client for the duration
 *   'q'  end the current UDP test and reply with an 'S' stats datagram
 * The board's own datagrams carry command 'd'.
 *
 * The 'S' reply describes the last test, TCP or UDP, so the host can fetch
 * the board's counters after a TCP test too. Each finished test is also
 * printed on the console. Ctrl-C on the console stops the server. */
#define SPEED_PORT          9
#define SPEED_MAGIC         0x57U       /* 'W' */
#define SPEED_HDR_LEN       16
#define SPEED_UDP_MAX       1472U       /* largest unfragmented payload */
#define SPEED_TCP_CHUNK     1460U
#define SPEED_BURST         32
#define SPEED_PROTO_TCP     1U
#define SPEED_PROTO_UDP     2U
#define SPEED_REPLY_LEN     108
/* A TCP client that sends no direction byte this long after it connects
 * gets a bidir test */
#define SPEED_TCP_MODE_MS   500U

/* How often the server checks for Ctrl-C on the console */
#define SPEED_ABORT_POLL_MS 100U
#ifdef WOLFIP_PORT_MCXE_ENET
#define SPEED_ABORT()       wolfip_test_abort()
#else
#define SPEED_ABORT()       0
#endif

#if defined(WOLFIP_PORT_MCXE_ENET) && NXP_MCXE_ENET_STATS
/* The driver enables the DWT cycle counter */
#define SPEED_PROFILE       1
#define SPEED_CYCLES()      (*(volatile uint32_t *)0xE0001004UL)
#else
#define SPEED_PROFILE       0
#define SPEED_CYCLES()      0U
#endif

struct speed_state {
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t cyc_total;     /* core cycles in the test window */
    uint64_t cyc_busy;      /* of which in wolfIP_poll() and socket calls */
    uint64_t start_ms;
    uint64_t end_ms;
    uint64_t stream_end_ms;
    uint32_t rx_pkts;
    uint32_t tx_pkts;
    uint32_t tx_seq;
    uint32_t stream_len;
    uint32_t polls;         /* wolfIP_poll() calls in the test window */
    uint32_t session;       /* UDP test id from the host, 0 for TCP */
    uint8_t  proto;
    uint8_t  mode;          /* 'r', 't', 'b'; 0 until a TCP client sends */
    uint8_t  active;
    uint8_t  streaming;
};

static int speed_listen_fd = -1;
static int speed_client_fd = -1;
static int speed_udp_fd = -1;
static struct speed_state speed;
static uint32_t speed_ended_session;    /* last UDP test that ended */
static struct wolfIP_sockaddr_in speed_peer;
static uint8_t speed_rxbuf[SPEED_UDP_MAX];
static uint8_t speed_txbuf[SPEED_UDP_MAX];
#if SPEED_PROFILE
static struct nxp_mcxe_enet_stats speed_drv;
#endif

static void speed_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void speed_put64(uint8_t *p, uint64_t v)
{
    speed_put32(p, (uint32_t)(v >> 32));
    speed_put32(p + 4, (uint32_t)v);
}

static uint32_t speed_get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static const char *speed_mode_name(uint8_t mode)
{
    if (mode == 'r')
        return "rx";
    if (mode == 't')
        return "tx";
    if (mode == 'b')
        return "bidir";
    return "?";
}

static uint32_t speed_kbps(uint64_t bytes, uint32_t ms)
{
    return (uint32_t)((bytes * 8ULL) / ms);
}

#if SPEED_PROFILE
/* part/total in tenths of a percent */
static uint32_t speed_permille(uint64_t part, uint64_t total)
{
    if (total == 0U)
        return 0U;
    return (uint32_t)((part * 1000ULL) / total);
}

static void speed_print_pct(const char *name, uint64_t part)
{
    uint32_t pm = speed_permille(part, speed.cyc_total);

    wolfBoot_printf(" %s %u.%u%%", name, (unsigned)(pm / 10U),
        (unsigned)(pm % 10U));
}
#endif

static void speed_print(void)
{
    uint32_t ms = (uint32_t)(speed.end_ms - speed.start_ms);
#if SPEED_PROFILE
    uint64_t drv = speed_drv.rx_cycles + speed_drv.tx_cycles +
                   speed_drv.idle_cycles;
    uint64_t stack = (speed.cyc_busy > drv) ? speed.cyc_busy - drv : 0U;
    uint32_t frames = speed_drv.rx_frames + speed_drv.tx_frames;
#endif

    if (ms == 0U)
        ms = 1U;
    wolfBoot_printf("SPEED %s %s: %u ms  rx %u B %u kbit/s  tx %u B %u kbit/s\r\n",
        (speed.proto == SPEED_PROTO_TCP) ? "tcp" : "udp",
        speed_mode_name(speed.mode), (unsigned)ms,
        (unsigned)speed.rx_bytes, (unsigned)speed_kbps(speed.rx_bytes, ms),
        (unsigned)speed.tx_bytes, (unsigned)speed_kbps(speed.tx_bytes, ms));
    if (speed.proto == SPEED_PROTO_UDP) {
        wolfBoot_printf("  datagrams rx %u tx %u\r\n",
            (unsigned)speed.rx_pkts, (unsigned)speed.tx_pkts);
    }
#if SPEED_PROFILE
    wolfBoot_printf("  cpu %u MHz:", (unsigned)(speed_drv.core_hz / 1000000U));
    speed_print_pct("drv-rx", speed_drv.rx_cycles);
    speed_print_pct("drv-tx", speed_drv.tx_cycles);
    speed_print_pct("drv-idle", speed_drv.idle_cycles);
    speed_print_pct("wolfIP", stack);
    speed_print_pct("loop", speed.cyc_total - speed.cyc_busy);
    wolfBoot_printf("\r\n  of which:");
    speed_print_pct("copy", speed_drv.copy_cycles);
    speed_print_pct("tx-ring-full", speed_drv.tx_wait_cycles);
    wolfBoot_printf("\r\n  frames rx %u tx %u, wolfIP %u cycles/frame,"
        " %u polls, %u cycles/poll\r\n",
        (unsigned)speed_drv.rx_frames, (unsigned)speed_drv.tx_frames,
        (unsigned)((frames > 0U) ? (stack / frames) : 0U),
        (unsigned)speed.polls,
        (unsigned)((speed.polls > 0U) ? (speed.cyc_busy / speed.polls) : 0U));
#endif
}

static void speed_start(uint8_t proto, uint8_t mode, uint64_t now)
{
    zmem(&speed, sizeof(speed));
    speed.proto = proto;
    speed.mode = mode;
    speed.start_ms = now;
    speed.active = 1U;
#if SPEED_PROFILE
    nxp_mcxe_enet_stats_reset();
#endif
}

static void speed_stop(uint64_t now)
{
    if (speed.active == 0U)
        return;
    speed.active = 0U;
    speed.streaming = 0U;
    speed.end_ms = now;
    speed_ended_session = speed.session;
#if SPEED_PROFILE
    nxp_mcxe_enet_stats_get(&speed_drv);
#endif
    speed_print();
}

/* Send the 'S' stats datagram for the last (or running) test. */
static void speed_reply(struct wolfIP *s, const struct wolfIP_sockaddr_in *to,
                        uint64_t now)
{
    uint8_t r[SPEED_REPLY_LEN];
    uint64_t end = (speed.active != 0U) ? now : speed.end_ms;

    zmem(r, sizeof(r));
    r[0] = SPEED_MAGIC;
    r[1] = 'S';
    r[2] = 1U;                  /* layout version */
    r[3] = speed.proto;
    r[4] = speed.mode;
    r[5] = speed.active;
    speed_put32(r + 8, (uint32_t)(end - speed.start_ms));
    speed_put32(r + 16, speed.rx_pkts);
    speed_put32(r + 20, speed.tx_pkts);
    speed_put64(r + 32, speed.rx_bytes);
    speed_put64(r + 40, speed.tx_bytes);
    speed_put64(r + 48, speed.cyc_total);
    speed_put64(r + 56, speed.cyc_busy);
    speed_put32(r + 104, speed.polls);
#if SPEED_PROFILE
    if (speed.active != 0U)
        nxp_mcxe_enet_stats_get(&speed_drv);
    speed_put32(r + 12, speed_drv.core_hz);
    speed_put32(r + 24, speed_drv.rx_frames);
    speed_put32(r + 28, speed_drv.tx_frames);
    speed_put64(r + 64, speed_drv.rx_cycles);
    speed_put64(r + 72, speed_drv.tx_cycles);
    speed_put64(r + 80, speed_drv.idle_cycles);
    speed_put64(r + 88, speed_drv.copy_cycles);
    speed_put64(r + 96, speed_drv.tx_wait_cycles);
#endif
    (void)wolfIP_sock_sendto(s, speed_udp_fd, r, sizeof(r), 0,
        (const struct wolfIP_sockaddr *)to, sizeof(*to));
}

/* Accept one TCP client at a time; a close ends the test. */
static void speed_cb(int fd, uint16_t event, void *arg)
{
    struct wolfIP *s = (struct wolfIP *)arg;
    int c;

    if (fd == speed_listen_fd) {
        if ((event & CB_EVENT_READABLE) != 0U) {
            c = wolfIP_sock_accept(s, speed_listen_fd, NULL, NULL);
            if (c >= 0) {
                if (speed_client_fd >= 0 || speed.active != 0U) {
                    /* one test at a time, TCP or UDP */
                    (void)wolfIP_sock_close(s, c);
                }
                else {
                    speed_client_fd = c;
                    wolfIP_register_callback(s, c, speed_cb, s);
                    speed_start(SPEED_PROTO_TCP, 0U, now_ms());
                }
            }
        }
        return;
    }
    if (fd == speed_client_fd && (event & CB_EVENT_CLOSED) != 0U) {
        speed_stop(now_ms());
        (void)wolfIP_sock_close(s, fd);
        speed_client_fd = -1;
    }
}

static void speed_tcp_pump(struct wolfIP *s, uint64_t now)
{
    int n;
    int k;

    if (speed_client_fd < 0)
        return;
    for (k = 0; k < SPEED_BURST; k++) {
        n = wolfIP_sock_recvfrom(s, speed_client_fd, speed_rxbuf,
            SPEED_TCP_CHUNK, 0, NULL, NULL);
        if (n <= 0)
            break;
        if (speed.mode == 0U) {
            if (speed_rxbuf[0] == 'r' || speed_rxbuf[0] == 't') {
                speed.mode = speed_rxbuf[0];
                n--;            /* the direction byte is not data */
            }
            else {
                speed.mode = 'b';
            }
        }
        speed.rx_bytes += (uint32_t)n;
    }
    if (speed.mode == 0U && now >= speed.start_ms + SPEED_TCP_MODE_MS)
        speed.mode = 'b';
    if (speed.mode != 't' && speed.mode != 'b')
        return;
    for (k = 0; k < SPEED_BURST; k++) {
        n = wolfIP_sock_send(s, speed_client_fd, speed_txbuf,
            SPEED_TCP_CHUNK, 0);
        if (n <= 0)
            break;
        speed.tx_bytes += (uint32_t)n;
    }
}

static void speed_udp_recv(struct wolfIP *s, uint64_t now)
{
    struct wolfIP_sockaddr_in from;
    socklen_t from_len;
    uint32_t len;
    uint32_t session;
    uint8_t cmd;
    int n;
    int k;

    for (k = 0; k < SPEED_BURST; k++) {
        from_len = sizeof(from);
        n = wolfIP_sock_recvfrom(s, speed_udp_fd, speed_rxbuf,
            sizeof(speed_rxbuf), 0, (struct wolfIP_sockaddr *)&from,
            &from_len);
        if (n <= 0)
            break;
        if (n < SPEED_HDR_LEN || speed_rxbuf[0] != SPEED_MAGIC)
            continue;
        cmd = speed_rxbuf[1];
        if (cmd == 'q') {
            if (speed.proto == SPEED_PROTO_UDP)
                speed_stop(now);
            speed_reply(s, &from, now);
            continue;
        }
        if (cmd != 'r' && cmd != 'b' && cmd != 't')
            continue;
        if (speed.active != 0U && speed.proto == SPEED_PROTO_TCP)
            continue;           /* a TCP test is running */
        session = speed_get32(speed_rxbuf + 12);
        if (session == 0U || session == speed_ended_session)
            continue;           /* left over from a finished test */
        if (speed.active != 0U && session != speed.session)
            speed_stop(now);    /* the host started a new test */
        if (speed.active == 0U) {
            speed_start(SPEED_PROTO_UDP, cmd, now);
            speed.session = session;
            speed_peer = from;
            if (cmd != 'r') {
                len = ((uint32_t)speed_rxbuf[2] << 8) | speed_rxbuf[3];
                if (len < (uint32_t)SPEED_HDR_LEN || len > SPEED_UDP_MAX)
                    len = SPEED_UDP_MAX;
                speed.stream_len = len;
                speed.stream_end_ms = now + speed_get32(speed_rxbuf + 8);
                speed.streaming = 1U;
            }
        }
        if (cmd != 't') {
            speed.rx_bytes += (uint32_t)n;
            speed.rx_pkts++;
        }
    }
}

static void speed_udp_send(struct wolfIP *s, uint64_t now)
{
    int n;
    int k;

    if (speed.streaming == 0U)
        return;
    if (now >= speed.stream_end_ms) {
        /* The host sends for the same duration, from slightly earlier, so
         * a bidir test ends here too: its window then holds no idle tail
         * while the host collects the last datagrams before 'q'. */
        speed_stop(now);
        return;
    }
    speed_txbuf[0] = SPEED_MAGIC;
    speed_txbuf[1] = 'd';
    speed_txbuf[2] = 0U;
    speed_txbuf[3] = 0U;
    speed_put32(speed_txbuf + 8, 0U);
    speed_put32(speed_txbuf + 12, speed.session);
    for (k = 0; k < SPEED_BURST; k++) {
        speed_put32(speed_txbuf + 4, speed.tx_seq);
        n = wolfIP_sock_sendto(s, speed_udp_fd, speed_txbuf,
            speed.stream_len, 0, (const struct wolfIP_sockaddr *)&speed_peer,
            sizeof(speed_peer));
        if (n <= 0)
            break;
        speed.tx_seq++;
        speed.tx_pkts++;
        speed.tx_bytes += (uint32_t)n;
    }
}

static int speed_open(struct wolfIP *s, int type)
{
    struct wolfIP_sockaddr_in addr;
    int fd;

    fd = wolfIP_sock_socket(s, AF_INET, type, 0);
    if (fd < 0)
        return -1;
    zmem(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = ee16(SPEED_PORT);
    if (wolfIP_sock_bind(s, fd, (struct wolfIP_sockaddr *)&addr,
            sizeof(addr)) < 0 ||
            (type == IPSTACK_SOCK_STREAM &&
             wolfIP_sock_listen(s, fd, 1) < 0)) {
        (void)wolfIP_sock_close(s, fd);
        return -1;
    }
    return fd;
}

/* Close the server's sockets, ending a running test first. */
static void speed_close(struct wolfIP *s)
{
    speed_stop(now_ms());
    if (speed_client_fd >= 0)
        (void)wolfIP_sock_close(s, speed_client_fd);
    if (speed_listen_fd >= 0)
        (void)wolfIP_sock_close(s, speed_listen_fd);
    if (speed_udp_fd >= 0)
        (void)wolfIP_sock_close(s, speed_udp_fd);
    speed_client_fd = -1;
    speed_listen_fd = -1;
    speed_udp_fd = -1;
}

static int run_speed_server(struct wolfIP *s)
{
    uint64_t now;
    uint64_t abort_check;
    uint32_t c_prev;
    uint32_t c_poll;
    uint32_t c_done;

    speed_listen_fd = speed_open(s, IPSTACK_SOCK_STREAM);
    speed_udp_fd = speed_open(s, IPSTACK_SOCK_DGRAM);
    if (speed_listen_fd < 0 || speed_udp_fd < 0) {
        wolfBoot_printf("SPEED: cannot open port %d\r\n", SPEED_PORT);
        speed_close(s);
        return -1;
    }
    wolfIP_register_callback(s, speed_listen_fd, speed_cb, s);
    wolfBoot_printf("SPEED: TCP and UDP throughput server on port %d\r\n",
        SPEED_PORT);
    wolfBoot_printf("  host: python3 tools/scripts/wolfip_speed.py <ip>\r\n");
    wolfBoot_printf("  Ctrl-C stops the server\r\n");

    abort_check = now_ms() + SPEED_ABORT_POLL_MS;
    c_prev = SPEED_CYCLES();
    for (;;) {
        now = now_ms();
        if (now >= abort_check) {
            abort_check = now + SPEED_ABORT_POLL_MS;
            if (SPEED_ABORT() != 0)
                break;
        }
        c_poll = SPEED_CYCLES();
        (void)wolfIP_poll(s, now);
        speed_tcp_pump(s, now);
        speed_udp_recv(s, now);
        speed_udp_send(s, now);
        c_done = SPEED_CYCLES();
        if (speed.active != 0U) {
            speed.cyc_busy += c_done - c_poll;
            speed.cyc_total += c_done - c_prev;
            speed.polls++;
        }
        c_prev = c_done;
    }
    speed_close(s);
    wolfBoot_printf("SPEED: stopped\r\n");
    return 0;
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
    return run_speed_server(s); /* until Ctrl-C */
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

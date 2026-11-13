#!/usr/bin/env python3
"""Host side of the wolfBoot test-app wolfIP throughput test.

Build the test-app with ENABLE_WOLFIP=1 WOLFBOOT_TEST_TFTP=0
WOLFIP_SPEED_TEST=1, boot it, then run from a host on the same network:

    python3 tools/scripts/wolfip_speed.py <board-ip> [--time 10] [--tests ...]

Directions are named from the board: rx = the board receives, tx = the
board sends, bidir = both at once. Each test runs over TCP or UDP port 9
(protocol in test-app/wolfip_tftp_test.c), then fetches the board's own
counters with a UDP query, including the CPU breakdown when the port
driver provides cycle counters (NXP MCX E247: NXP_MCXE_ENET_STATS).

Reported rates: "rx" is what the board received, timed by the board; "tx"
is what this host received from the board, timed by the host. UDP loss is
datagrams sent but not received, per direction.

Standard library only; runs on Linux, macOS and Windows.
"""

import argparse
import socket
import struct
import sys
import threading
import time

MAGIC = 0x57
HDR = struct.Struct(">BBHII")           # magic, cmd, size, seq, dur_ms
REPLY_LEN = 108
TESTS = ["tcp-rx", "tcp-tx", "tcp-bidir", "udp-rx", "udp-tx", "udp-bidir"]


class Stats:
    """Decoded 'S' reply (layout version 1)."""

    def __init__(self, data):
        (self.proto, self.mode, self.active) = struct.unpack_from(">BBB", data, 3)
        (self.ms, self.hz, self.rx_pkts, self.tx_pkts, self.rx_frames,
         self.tx_frames) = struct.unpack_from(">6I", data, 8)
        (self.rx_bytes, self.tx_bytes, self.cyc_total, self.cyc_busy,
         self.drv_rx, self.drv_tx, self.drv_idle, self.copy,
         self.tx_wait) = struct.unpack_from(">9Q", data, 32)
        (self.polls,) = struct.unpack_from(">I", data, 104)

    def pct(self, cycles):
        return 100.0 * cycles / self.cyc_total if self.cyc_total else 0.0

    def cpu(self):
        """CPU shares in %, or None when the board has no cycle counters."""
        if self.hz == 0 or self.cyc_total == 0:
            return None
        drv = self.drv_rx + self.drv_tx + self.drv_idle
        return {
            "drv-rx": self.pct(self.drv_rx),
            "drv-tx": self.pct(self.drv_tx),
            "drv-idle": self.pct(self.drv_idle),
            "wolfIP": self.pct(max(self.cyc_busy - drv, 0)),
            "loop": self.pct(self.cyc_total - self.cyc_busy),
            "copy": self.pct(self.copy),
            "ring-full": self.pct(self.tx_wait),
        }


def mbps(nbytes, secs):
    return nbytes * 8.0 / secs / 1e6 if secs > 0 else 0.0


def udp_socket():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 << 20)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 20)
    return s


def query(sock, addr, want_proto, timeout=5.0):
    """Send 'q' until the board reports the finished test of want_proto."""
    deadline = time.monotonic() + timeout
    sock.settimeout(0.3)
    while time.monotonic() < deadline:
        sock.sendto(HDR.pack(MAGIC, ord("q"), 0, 0, 0), addr)
        end = time.monotonic() + 0.3
        while time.monotonic() < end:
            try:
                data, _ = sock.recvfrom(2048)
            except socket.timeout:
                break
            if len(data) >= REPLY_LEN and data[0] == MAGIC and data[1] == ord("S"):
                st = Stats(data)
                if st.active == 0 and st.proto == want_proto:
                    return st
    raise RuntimeError("no stats reply from the board")


def drain(sock):
    sock.settimeout(0)
    try:
        while True:
            sock.recvfrom(2048)
    except (BlockingIOError, socket.error):
        pass


class Pacer:
    """Spread sends to rate_mbps (0 = as fast as possible)."""

    def __init__(self, rate_mbps, size):
        self.gap = size * 8.0 / (rate_mbps * 1e6) if rate_mbps > 0 else 0.0
        self.next = time.perf_counter()

    def wait(self):
        if self.gap == 0.0:
            return
        self.next += self.gap
        while time.perf_counter() < self.next:
            pass


def tcp_test(ip, port, mode, secs):
    """Returns (host bytes sent, host bytes received, host seconds)."""
    s = socket.create_connection((ip, port), timeout=5)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.sendall(mode.encode())
    sent = [0]
    recvd = [0]
    stop = time.monotonic() + secs
    buf = bytes(65536)

    def sender():
        while time.monotonic() < stop:
            s.sendall(buf)
            sent[0] += len(buf)

    def receiver():
        s.settimeout(0.5)
        while time.monotonic() < stop:
            try:
                n = len(s.recv(65536))
            except socket.timeout:
                continue
            if n == 0:
                break
            recvd[0] += n

    t0 = time.monotonic()
    workers = []
    if mode in ("r", "b"):
        workers.append(threading.Thread(target=sender))
    if mode in ("t", "b"):
        workers.append(threading.Thread(target=receiver))
    for w in workers:
        w.start()
    for w in workers:
        w.join()
    elapsed = time.monotonic() - t0
    s.close()
    return sent[0], recvd[0], elapsed


def udp_test(sock, addr, mode, secs, size, rate):
    """Returns (datagrams sent, datagrams received, bytes received, secs)."""
    dur_ms = int(secs * 1000)
    sent = [0]
    got = [0, 0]                        # datagrams, bytes
    first = [None]
    last = [None]
    stop_rx = threading.Event()

    def receiver():
        sock.settimeout(0.2)
        idle_limit = 1.0
        while not stop_rx.is_set():
            try:
                data, _ = sock.recvfrom(2048)
            except socket.timeout:
                if last[0] is not None and time.monotonic() - last[0] > idle_limit:
                    break
                continue
            if len(data) >= HDR.size and data[0] == MAGIC and data[1] == ord("d"):
                now = time.monotonic()
                if first[0] is None:
                    first[0] = now
                last[0] = now
                got[0] += 1
                got[1] += len(data)

    payload = bytearray(size)
    t0 = time.monotonic()
    rx = None
    if mode in ("t", "b"):
        rx = threading.Thread(target=receiver)
        rx.start()
    if mode == "t":
        sock.sendto(HDR.pack(MAGIC, ord("t"), size, 0, dur_ms), addr)
        time.sleep(1.0)
        if got[0] == 0:                 # start lost: try once more
            sock.sendto(HDR.pack(MAGIC, ord("t"), size, 0, dur_ms), addr)
    else:
        cmd = ord(mode)
        pacer = Pacer(rate, size)
        stop = t0 + secs
        seq = 0
        while time.monotonic() < stop:
            HDR.pack_into(payload, 0, MAGIC, cmd, size, seq, dur_ms)
            try:
                sock.sendto(payload, addr)
            except (BlockingIOError, OSError):
                continue
            seq += 1
            pacer.wait()
        sent[0] = seq
    if rx is not None:
        rx.join(timeout=secs + 5)
        stop_rx.set()
        rx.join()
    span = (last[0] - first[0]) if (first[0] is not None and last[0] != first[0]) else (time.monotonic() - t0)
    return sent[0], got[0], got[1], span


def run_test(name, args):
    proto, direction = name.split("-")
    mode = {"rx": "r", "tx": "t", "bidir": "b"}[direction]
    addr = (args.ip, args.port)
    ctl = udp_socket()
    try:
        drain(ctl)
        if proto == "tcp":
            h_sent, h_recv, h_secs = tcp_test(args.ip, args.port, mode, args.time)
            st = query(ctl, addr, 1)
            host_tx = mbps(h_recv, h_secs)
            loss_rx = loss_tx = None
        else:
            # End any UDP test left running, so this one starts fresh
            ctl.sendto(HDR.pack(MAGIC, ord("q"), 0, 0, 0), addr)
            time.sleep(0.3)
            drain(ctl)
            sent, got, got_bytes, span = udp_test(ctl, addr, mode, args.time,
                                                  args.size, args.rate)
            st = query(ctl, addr, 2)
            host_tx = mbps(got_bytes, span)
            loss_rx = (100.0 * (sent - st.rx_pkts) / sent) if sent else None
            loss_tx = (100.0 * (st.tx_pkts - got) / st.tx_pkts) if st.tx_pkts else None
    finally:
        ctl.close()
    board_secs = st.ms / 1000.0
    return {
        "test": name,
        "rx": mbps(st.rx_bytes, board_secs) if mode != "t" else None,
        "tx": host_tx if mode != "r" else None,
        "loss_rx": loss_rx if mode != "t" else None,
        "loss_tx": loss_tx if mode != "r" else None,
        "stats": st,
    }


def fmt(v, spec="{:6.2f}", width=6):
    return spec.format(v) if v is not None else "-".rjust(width)


def print_table(results):
    cpu_cols = ["drv-rx", "drv-tx", "drv-idle", "wolfIP", "loop", "copy"]
    print()
    print("Mbit/s: rx = received by the board, tx = sent by the board."
          " CPU in % of the test window.")
    head = "{:<10} {:>6} {:>6} {:>7} {:>7}".format(
        "test", "rx", "tx", "rxloss%", "txloss%")
    has_cpu = any(r["stats"].cpu() is not None for r in results)
    if has_cpu:
        head += "  " + " ".join("{:>8}".format(c) for c in cpu_cols)
    print(head)
    for r in results:
        line = "{:<10} {} {} {} {}".format(
            r["test"], fmt(r["rx"]), fmt(r["tx"]),
            fmt(r["loss_rx"], "{:7.1f}", 7), fmt(r["loss_tx"], "{:7.1f}", 7))
        cpu = r["stats"].cpu()
        if cpu is not None:
            line += "  " + " ".join("{:8.1f}".format(cpu[c]) for c in cpu_cols)
        print(line)
    for r in results:
        st = r["stats"]
        if st.hz:
            frames = st.rx_frames + st.tx_frames
            drv = st.drv_rx + st.drv_tx + st.drv_idle
            stack = max(st.cyc_busy - drv, 0)
            print("{:<10} {} MHz, frames rx {} tx {}, cycles/frame: wolfIP {}, "
                  "drv-rx {}, drv-tx {}; {} polls, {} cycles/poll".format(
                      r["test"], st.hz // 1000000, st.rx_frames, st.tx_frames,
                      stack // frames if frames else 0,
                      st.drv_rx // st.rx_frames if st.rx_frames else 0,
                      st.drv_tx // st.tx_frames if st.tx_frames else 0,
                      st.polls, st.cyc_busy // st.polls if st.polls else 0))


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("ip", help="board IP address")
    p.add_argument("--port", type=int, default=9)
    p.add_argument("--time", type=float, default=10.0, help="seconds per test")
    p.add_argument("--size", type=int, default=1472,
                   help="UDP datagram payload bytes (12..1472)")
    p.add_argument("--rate", type=float, default=0.0,
                   help="UDP send rate to the board in Mbit/s (0 = unlimited)")
    p.add_argument("--tests", default=",".join(TESTS),
                   help="comma separated subset of: " + ",".join(TESTS))
    p.add_argument("--pause", type=float, default=1.0,
                   help="seconds between tests")
    args = p.parse_args()

    if not 12 <= args.size <= 1472:
        p.error("--size must be 12..1472")
    names = [t.strip() for t in args.tests.split(",") if t.strip()]
    for t in names:
        if t not in TESTS:
            p.error("unknown test " + t)

    results = []
    for i, name in enumerate(names):
        if i:
            time.sleep(args.pause)
        print("running {} for {:g} s...".format(name, args.time), flush=True)
        try:
            results.append(run_test(name, args))
        except (OSError, RuntimeError) as e:
            print("  {} failed: {}".format(name, e), file=sys.stderr)
    if results:
        print_table(results)
    return 0 if len(results) == len(names) else 1


if __name__ == "__main__":
    sys.exit(main())

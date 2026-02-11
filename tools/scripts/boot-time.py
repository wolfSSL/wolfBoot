#!/usr/bin/env python3
import select
import time
from datetime import datetime, timedelta

import gpiod
from gpiod.line import Direction, Edge, Bias, Value

CHIP = "/dev/gpiochip0"
GPIO2 = 2
GPIO4 = 4

HOLD_LOW_S = 0.5
DEBOUNCE_MS_GPIO4 = 0  # set e.g. to 100 if you want

def now():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def value_to_01(v: Value) -> int:
    return 1 if v == Value.ACTIVE else 0

def get_val(req, off: int) -> int:
    return value_to_01(req.get_value(off))

def dump_vals(req, offs):
    parts = []
    for o in offs:
        try:
            parts.append(f"{o}={get_val(req, o)}")
        except Exception as e:
            parts.append(f"{o}=? ({e})")
    return ", ".join(parts)

def drain_events(req):
    try:
        req.read_edge_events()
    except Exception as e:
        print(f"Warning: failed to read edge events: {e}")

def main():

    cfg_initial = {
        (GPIO2,): gpiod.LineSettings(
            direction=Direction.OUTPUT,
            output_value=Value.INACTIVE,  # 0
        ),
        (GPIO4,): gpiod.LineSettings(
            direction=Direction.INPUT,
            bias=Bias.PULL_DOWN,
            edge_detection=Edge.RISING,
            debounce_period=timedelta(milliseconds=DEBOUNCE_MS_GPIO4),
        ),
    }

    with gpiod.request_lines(CHIP, consumer="release-to-edge", config=cfg_initial) as req:

        # Ensure we don't accidentally read an earlier GPIO4 edge
        #drain_events(req)

        time.sleep(HOLD_LOW_S)


        # Reconfigure GPIO2 to input (no edge needed on GPIO2)
        cfg_release = {
            (GPIO2,): gpiod.LineSettings(
                direction=Direction.INPUT,
                bias=Bias.DISABLED,      # keep bias none (you said bias not working anyway)
                edge_detection=Edge.NONE,
            ),
            (GPIO4,): None,  # unchanged
        }

        # Take a best-effort timestamp around the reconfigure call.
        # Using midpoint reduces the error to roughly half of the call duration.
        t_before = time.monotonic_ns()
        req.reconfigure_lines(cfg_release)
        t_after = time.monotonic_ns()
        t0 = (t_before + t_after) // 2


        t4 = None
        while t4 is None:
            select.select([req.fd], [], [])
            for ev in req.read_edge_events():
                if ev.line_offset == GPIO4 and ev.event_type == gpiod.EdgeEvent.Type.RISING_EDGE:
                    t4 = ev.timestamp_ns
                    break

        delta_ns = t4 - t0
        print(f"{delta_ns/1e6:.3f} ms")

if __name__ == "__main__":
    main()

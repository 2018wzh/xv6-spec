#!/usr/bin/env python3
"""Stop VisionFive 2 U-Boot at its command prompt.

Usage:
  python3 tools/vf2_uboot_stop.py [serial-port]

The operator resets the board first. This script watches the serial line for
the autoboot countdown, keeps asserting carriage-return through the whole
countdown, and exits with status 0 as soon as the U-Boot prompt is observed.
U-Boot stays at the prompt afterwards, ready for manual commands.
"""

from __future__ import annotations

import sys
import time

import serial

PROMPTS = (b"StarFive #", b"VisionFive2 #", b"JH7110 #")
INTERRUPT_MARKERS = (b"Hit any key to stop autoboot", b"Autoboot in progress",
                     b"bootdelay")


def main() -> int:
    port_name = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
    interrupt_seen = False
    last_burst = 0.0
    deadline = time.monotonic() + 120
    with serial.Serial(port_name, 115200, timeout=0.25, exclusive=True) as port:
        transcript = bytearray()
        while port.in_waiting:
            transcript.extend(port.read(port.in_waiting))
        while time.monotonic() < deadline:
            chunk = port.read(port.in_waiting or 1)
            if chunk:
                transcript.extend(chunk)
                for prompt in PROMPTS:
                    if prompt in transcript:
                        print(f"vf2_uboot_stop: prompt {prompt.decode()} ready",
                              flush=True)
                        return 0
            if any(marker in transcript for marker in INTERRUPT_MARKERS):
                if not interrupt_seen:
                    print("vf2_uboot_stop: autoboot countdown seen", flush=True)
                interrupt_seen = True
            if interrupt_seen and time.monotonic() - last_burst >= 0.15:
                port.write(b"\r")
                port.flush()
                last_burst = time.monotonic()
    print("vf2_uboot_stop: no U-Boot prompt observed", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())

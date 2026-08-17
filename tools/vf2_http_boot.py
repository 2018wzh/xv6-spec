#!/usr/bin/env python3
"""Load build/xv6.itb over U-Boot wget (HTTP) and boot it.

Precondition: U-Boot is idle at StarFive #. The host serves the build
directory over HTTP on port 8080. This script configures the board-side
static address 192.168.11.2, downloads the FIT, verifies its CRC, and boots
it without touching the SD FAT partition.
"""

from __future__ import annotations

import os
import pathlib
import subprocess
import time
import zlib

import serial

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIT = ROOT / "build" / "xv6.itb"
PROMPT = b"StarFive #"


def send(port: serial.Serial, line: str) -> None:
    port.write(line.encode() + b"\r")
    port.flush()


def wait_prompt(port: serial.Serial, timeout: float = 60) -> bool:
    buf = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            buf += chunk
            if PROMPT in buf:
                return True
    return False


def main() -> int:
    port_name = os.environ.get("VOS_VF2_SERIAL_PORT", "/dev/ttyUSB0")
    host = os.environ.get("VOS_VF2_HTTP_HOST", "192.168.11.1")
    size = FIT.stat().st_size
    host_crc = zlib.crc32(FIT.read_bytes()) & 0xFFFFFFFF
    with serial.Serial(port_name, 115200, timeout=0.15, exclusive=True) as port:
        for cmd in ("setenv ipaddr 192.168.11.2",
                    "setenv netmask 255.255.255.0",
                    "setenv serverip " + host):
            send(port, cmd)
            wait_prompt(port, 10)
        send(port, f"wget 0x44000000 http://{host}:8080/xv6.itb")
        if not wait_prompt(port, 60):
            return 1
        send(port, f"crc32 0x44000000 {size:#x}")
        crcbuf = b""
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            chunk = port.read(port.in_waiting or 1)
            if chunk:
                crcbuf += chunk
                if PROMPT in crcbuf:
                    break
        crc_text = crcbuf.decode(errors="replace")
        print(crc_text, flush=True)
        # Boot; keep capturing until shell, panic, or timeout.
        send(port, "bootm 0x44000000")
        boot = b""
        last = time.time()
        end = time.monotonic() + 240
        while time.monotonic() < end:
            chunk = port.read(port.in_waiting or 1)
            if chunk:
                boot += chunk
                last = time.time()
                print(chunk.decode(errors="replace"), end="", flush=True)
            if b"$ " in boot:
                print("\nSHELL PROMPT", flush=True)
                break
            if (b"panic" in boot or b"Unhandled exception" in boot) \
                    and time.monotonic() - last > 3:
                break
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Flash build/xv6.itb into the board's FAT partition from a U-Boot prompt.

Precondition: U-Boot is already idle at StarFive # (see
tools/vf2_uboot_stop.py). This script runs loady, sends the FIT with YMODEM,
writes it to the selected FAT partition with fatwrite, and leaves U-Boot at
the prompt. The official vos hardware runner performs the equivalent boot
flow over serial.
"""

from __future__ import annotations

import os
import pathlib
import struct
import time

import serial

PROMPTS = (b"StarFive #", b"VisionFive2 #", b"JH7110 #")
ROOT = pathlib.Path(__file__).resolve().parents[1]
FIT = ROOT / "build" / "xv6.itb"


def crc16(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def packet(port: serial.Serial, seq: int, payload: bytes, size: int) -> None:
    payload = payload.ljust(size, b"\0")[:size]
    port.write(bytes([2 if size == 1024 else 1, seq & 0xFF, 0xFF - (seq & 0xFF)])
               + payload + struct.pack(">H", crc16(payload)))
    port.flush()


def wait_for(port: serial.Serial, wanted: set[int], timeout: float = 60) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        byte = port.read(1)
        if byte and byte[0] in wanted:
            return byte[0]
    raise TimeoutError(f"wanted {wanted}")


def wait_prompt(port: serial.Serial, timeout: float = 60) -> bool:
    buf = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            buf += chunk
            if any(buf.endswith(prompt) for prompt in PROMPTS):
                return True
    return False


def main() -> int:
    port_name = os.environ.get("VOS_VF2_SERIAL_PORT", "/dev/ttyUSB0")
    boot_part = os.environ.get("VOS_VF2_BOOT_PART", "3")
    data = FIT.read_bytes()
    size = len(data)
    with serial.Serial(port_name, 115200, timeout=0.15, exclusive=True) as port:
        port.write(b"\r")
        port.flush()
        time.sleep(0.4)
        while port.in_waiting:
            port.read(port.in_waiting)
        port.write(b"loady 0x44000000\r")
        port.flush()
        time.sleep(0.5)
        wait_for(port, {0x43})
        packet(port, 0, FIT.name.encode() + b"\0" + str(size).encode()
               + b"\0" + b"0 0\0", 128)
        response = wait_for(port, {0x06, 0x43, 0x15}, 30)
        if response == 0x15:
            raise SystemExit("NAK on YMODEM header")
        if response != 0x06:
            pass
        else:
            wait_for(port, {0x43}, 10)
        seq = 1
        for offset in range(0, size, 1024):
            packet(port, seq, data[offset:offset + 1024], 1024)
            response = wait_for(port, {0x06, 0x15}, 30)
            if response == 0x15:
                raise SystemExit(f"NAK on YMODEM block {seq}")
            seq += 1
        port.write(b"\x04")
        wait_for(port, {0x15, 0x06}, 30)
        port.write(b"\x04")
        wait_for(port, {0x06}, 30)
        packet(port, 0, b"", 128)
        try:
            wait_for(port, {0x06}, 10)
        except TimeoutError:
            pass
        if not wait_prompt(port, 20):
            port.write(b"\r")
            port.flush()
            wait_prompt(port, 15)
        port.write(b"fatrm mmc 1:" + boot_part.encode() + b" xv6.itb\r")
        port.flush()
        wait_prompt(port, 20)
        port.write(f"fatwrite mmc 1:{boot_part} 0x44000000 xv6.itb {size:#x}\r".encode())
        port.flush()
        if not wait_prompt(port, 60):
            return 1
        print(f"flashed {size} bytes to mmc 1:{boot_part}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

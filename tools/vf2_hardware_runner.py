#!/usr/bin/env python3
"""Drive U-Boot and xv6 over a VisionFive 2 serial console."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import re
import time

import serial

ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / ".vos" / "hardware" / "visionfive2-evidence.json"
SERIAL_LOG = ROOT / ".vos" / "hardware" / "visionfive2-serial.log"
BOARD = "visionfive2-v1.3b"


def required_env(name: str) -> str:
    value = os.environ.get(name, "")
    if not value or len(value) > 128 or any(ord(ch) < 32 for ch in value):
        raise SystemExit(f"{name} is missing or invalid")
    return value


def wait_for(port: serial.Serial, transcript: bytearray, pattern: bytes,
             timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            transcript.extend(chunk)
            if pattern in transcript:
                return
    raise TimeoutError(f"serial marker not observed: {pattern.decode(errors='replace')}")


def send(port: serial.Serial, command: str) -> None:
    port.write(command.encode("ascii") + b"\r\n")
    port.flush()


def main() -> None:
    serial_port = required_env("VOS_VF2_SERIAL_PORT")
    board_alias = required_env("VOS_VF2_BOARD_ALIAS")
    if board_alias != BOARD:
        raise SystemExit(f"unsupported VOS_VF2_BOARD_ALIAS: {board_alias}")
    transcript = bytearray()
    started = time.time()
    outcome = "failed"
    error = None
    try:
        with serial.Serial(serial_port, 115200, timeout=0.25,
                           write_timeout=5, exclusive=True) as port:
            port.write(b"\r")
            wait_for(port, transcript, b"StarFive #", 30)
            send(port, "mmc dev 1")
            wait_for(port, transcript, b"StarFive #", 15)
            send(port, "fatload mmc 1:1 0x44000000 xv6.itb")
            wait_for(port, transcript, b"StarFive #", 30)
            send(port, "bootm 0x44000000")
            wait_for(port, transcript, b"XV6_BOOT_OK", 60)
            for logical_hart in (1, 2, 3):
                wait_for(port, transcript,
                         f"hart {logical_hart} starting".encode("ascii"), 30)
            wait_for(port, transcript, b"$ ", 60)
            send(port, "usertests")
            wait_for(port, transcript, b"ALL TESTS PASSED", 1200)
            outcome = "passed"
    except Exception as exc:  # evidence records the exact failure class, then fails
        error = f"{type(exc).__name__}: {exc}"
    EVIDENCE.parent.mkdir(parents=True, exist_ok=True)
    redacted = bytes(transcript).replace(serial_port.encode(), b"<serial-port>")
    SERIAL_LOG.write_bytes(redacted)
    evidence = {
        "schema": "xv6.hardware-evidence.v1",
        "board_alias": board_alias,
        "outcome": outcome,
        "review_status": "pending_human_review",
        "started_unix": int(started),
        "duration_seconds": round(time.time() - started, 3),
        "serial_log_sha256": hashlib.sha256(redacted).hexdigest(),
        "markers": {
            "boot": b"XV6_BOOT_OK" in redacted,
            "four_u74_harts": all(f"hart {i} starting".encode() in redacted
                                   for i in (1, 2, 3)),
            "usertests": b"ALL TESTS PASSED" in redacted,
        },
        "error": error,
    }
    EVIDENCE.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    if outcome != "passed":
        raise SystemExit(error or "hardware verification failed")


if __name__ == "__main__":
    main()

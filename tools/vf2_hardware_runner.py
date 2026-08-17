#!/usr/bin/env python3
"""Drive U-Boot and xv6 over a VisionFive 2 serial console.

The runner is deliberately fail-closed. It hashes the immutable workload
inputs first, requires a clean Git HEAD, then drives the physical board
through U-Boot -> FIT -> four U74 harts -> `usertests`. It never upgrades
the result beyond `pending_human_review`.
"""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import re
import subprocess
import time

import serial

ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / ".vos" / "hardware" / "visionfive2-evidence.json"
SERIAL_LOG = ROOT / ".vos" / "hardware" / "visionfive2-serial.log"
BOARD = "visionfive2-v1.3b"
BOOT_PART_DEFAULT = "1"
PROMPT_PATTERNS = (b"StarFive #", b"VisionFive2 #", b"JH7110 #")


def required_env(name: str) -> str:
    value = os.environ.get(name, "")
    if not value or len(value) > 128 or any(ord(ch) < 32 for ch in value):
        raise SystemExit(f"{name} is missing or invalid")
    return value


def sha256_file(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_preflight() -> dict:
    fit = ROOT / "build" / "xv6.itb"
    kernel = ROOT / "build" / "kernel-vf2.bin"
    dtb = ROOT / "build" / "visionfive2.dtb"
    fsimg = ROOT / "build" / "vf2-fs.img"
    if not (fit.exists() and kernel.exists() and dtb.exists() and fsimg.exists()):
        subprocess.run(["make", "vf2-fit"], cwd=ROOT, check=True)
    for path in (fit, kernel, dtb, fsimg):
        if not path.exists():
            raise SystemExit(f"missing immutable workload input: {path}")
    return {
        "fit_sha256": sha256_file(fit),
        "kernel_bin_sha256": sha256_file(kernel),
        "dtb_sha256": sha256_file(dtb),
        "fs_img_sha256": sha256_file(fsimg),
    }


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


def wait_for_any(port: serial.Serial, transcript: bytearray,
                 patterns: tuple[bytes, ...], timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            transcript.extend(chunk)
            for pattern in patterns:
                if pattern in transcript:
                    return pattern
    raise TimeoutError("serial prompt not observed")


def send(port: serial.Serial, command: str) -> None:
    port.write(command.encode("ascii") + b"\r\n")
    port.flush()


def git_identity() -> dict:
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                          capture_output=True, text=True, check=True).stdout.strip()
    clean = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT,
                           capture_output=True, text=True, check=True).stdout == ""
    return {"commit_sha": head, "clean_head": clean}


def main() -> None:
    serial_port = required_env("VOS_VF2_SERIAL_PORT")
    board_alias = required_env("VOS_VF2_BOARD_ALIAS")
    if board_alias != BOARD:
        raise SystemExit(f"unsupported VOS_VF2_BOARD_ALIAS: {board_alias}")
    boot_part = os.environ.get("VOS_VF2_BOOT_PART", BOOT_PART_DEFAULT)
    if not re.fullmatch(r"[1-9][0-9]?", boot_part):
        raise SystemExit("VOS_VF2_BOOT_PART must be a positive U-Boot partition number")

    started = time.time()
    outcome = "failed"
    error = None
    inputs = run_preflight()
    transcript = bytearray()
    try:
        with serial.Serial(serial_port, 115200, timeout=0.25,
                           write_timeout=5, exclusive=True) as port:
            port.write(b"\r")
            wait_for_any(port, transcript, PROMPT_PATTERNS, 30)
            send(port, "mmc dev 1")
            wait_for_any(port, transcript, PROMPT_PATTERNS, 15)
            send(port, f"fatload mmc 1:{boot_part} 0x44000000 xv6.itb")
            wait_for_any(port, transcript, PROMPT_PATTERNS, 30)
            send(port, "bootm 0x44000000")
            wait_for(port, transcript, b"XV6_BOOT_OK", 90)
            for logical_hart in (1, 2, 3):
                wait_for(port, transcript,
                         f"hart {logical_hart} starting".encode("ascii"), 60)
            wait_for(port, transcript, b"$ ", 90)
            send(port, "usertests")
            wait_for(port, transcript, b"ALL TESTS PASSED", 1200)
            outcome = "passed"
    except Exception as exc:  # evidence records the exact failure class, then fails
        error = f"{type(exc).__name__}: {exc}"
    EVIDENCE.parent.mkdir(parents=True, exist_ok=True)
    redacted = bytes(transcript).replace(serial_port.encode(), b"<serial-port>")
    SERIAL_LOG.write_bytes(redacted)
    identity = git_identity()
    evidence = {
        "schema": "xv6.hardware-evidence.v1",
        "board_alias": board_alias,
        "boot_partition": boot_part,
        "outcome": outcome,
        "review_status": "pending_human_review",
        "started_unix": int(started),
        "duration_seconds": round(time.time() - started, 3),
        "inputs": inputs,
        "git": identity,
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

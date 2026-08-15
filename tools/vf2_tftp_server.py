#!/usr/bin/env python3
"""Read-only TFTP server for the VisionFive 2 U-Boot loop.

Binds an unprivileged UDP port (default 6969) and serves build/xv6.itb and
build/xv6.uImage. U-Boot's tftpdstp environment variable may be ignored by
some firmware builds, in which case run this script as root on port 69:
  sudo python3 tools/vf2_tftp_server.py 69

The server is intentionally resilient: client timeouts, duplicate ACKs,
port-unreachable ICMP errors, and malformed requests are logged and ignored
so a completed transfer can never take the process down.
"""

from __future__ import annotations

import pathlib
import socket
import struct
import sys
import time
import traceback

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def serve(port: int) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", port))
    print(f"tftp: serving {BUILD} on udp/{port}", flush=True)
    while True:
        try:
            data, peer = sock.recvfrom(4096)
            if len(data) < 2 or struct.unpack("!H", data[:2])[0] != 1:
                continue
            parts = data[2:].split(b"\0")
            filename = parts[0].decode(errors="replace")
            path = (BUILD / filename).resolve()
            if not path.is_relative_to(BUILD) or not path.is_file():
                sock.sendto(struct.pack("!HH", 5, 1) + b"not found\0", peer)
                print(f"tftp: not found {filename}", flush=True)
                continue
            payload = path.read_bytes()
            block = 1
            offset = 0
            retries = 0
            while True:
                chunk = payload[offset:offset + 512]
                sock.sendto(struct.pack("!HH", 3, block) + chunk, peer)
                if len(chunk) < 512:
                    # Wait briefly for the final ACK. Duplicate or missing
                    # ACKs are non-fatal; the transfer is already complete.
                    sock.settimeout(1.0)
                    try:
                        sock.recvfrom(4096)
                    except OSError:
                        pass
                    break
                sock.settimeout(2.0)
                try:
                    ack, _ = sock.recvfrom(4096)
                    if len(ack) >= 4:
                        opcode, ack_block = struct.unpack("!HH", ack[:4])
                        if opcode == 4 and ack_block == block:
                            block += 1
                            offset += 512
                            retries = 0
                            continue
                        # Duplicate or stale ACK: retransmit the current block.
                except socket.timeout:
                    pass
                retries += 1
                if retries > 10:
                    print(f"tftp: {peer[0]} stopped acknowledging "
                          f"{filename}", flush=True)
                    break
            print(f"tftp: sent {filename} ({len(payload)} bytes) to "
                  f"{peer[0]}", flush=True)
        except KeyboardInterrupt:
            raise
        except Exception:
            print("tftp: recovered from request error:", file=sys.stderr,
                  flush=True)
            traceback.print_exc(file=sys.stderr)
            time.sleep(0.2)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 6969
    serve(port)

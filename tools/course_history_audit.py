#!/usr/bin/env python3
"""Audit every published course tag as a complete, future-isolated Git tree."""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
TAGS = {
    1: "course/lab1-complete",
    2: "course/lab2-complete",
    3: "course/lab3-complete",
    4: "course/lab4-complete",
    5: "course/lab5-complete",
    6: "course/lab6-complete",
    7: "course/lab7-complete",
    8: "course/lab8-complete",
    9: "course/lab9-candidate",
    10: "course/lab10-candidate",
}

INTRODUCED_PATHS = {
    "kernel/": 2,
    "tests/": 2,
    "kernel/kalloc.c": 3,
    "kernel/vm.c": 3,
    "kernel/trap.c": 4,
    "kernel/plic.c": 4,
    "kernel/uart.c": 4,
    "kernel/proc.c": 5,
    "kernel/syscall.c": 5,
    "kernel/sysproc.c": 5,
    "kernel/bio.c": 6,
    "kernel/fs.c": 6,
    "kernel/log.c": 6,
    "kernel/exec.c": 6,
    "kernel/virtio_disk.c": 6,
    # The filesystem uses the internal file object in Lab 6; descriptor ABI
    # ownership is still introduced by InterfaceSpec in Lab 7.
    "kernel/file.c": 6,
    "kernel/pipe.c": 7,
    "kernel/sysfile.c": 7,
    "user/sh.c": 7,
    "spec/goals/": 8,
    "hardware/": 9,
    "third_party/": 9,
    "kernel/platform.c": 9,
    "kernel/sbi.c": 9,
    "kernel/sd.c": 9,
    "kernel/gpt.c": 9,
    "tools/fetch_vf2_assets.py": 9,
    "tools/vf2_image.py": 9,
    "tools/vf2_hardware_runner.py": 9,
    "tools/course_history_audit.py": 10,
}

FUTURE_TERMS = {
    1: r"\b(kalloc|Sv39|trap|timer|PLIC|UART|process|syscall|inode|virtio|pipe|shell|usertests|VisionFive|FDT|FIT|SDIO|GPT)\b",
    2: r"\b(kalloc|Sv39|trap|timer|PLIC|process|syscall|inode|virtio|pipe|shell|usertests|VisionFive|FDT|FIT|SDIO|GPT)\b",
    3: r"\b(trap|timer|PLIC|process|syscall|inode|virtio|pipe|shell|usertests|VisionFive|FDT|FIT|SDIO|GPT)\b",
    4: r"\b(fork|syscall|inode|virtio|pipe|shell|usertests|VisionFive|FDT|FIT|SDIO|GPT)\b",
    5: r"\b(buffer cache|inode|virtio|pipe|shell|usertests|VisionFive|FDT|FIT|SDIO|GPT)\b",
    6: r"\b(pipe|shell|usertests|VisionFive|FDT|FIT|SDIO|GPT)\b",
    7: r"\b(usertests_all_pass|VisionFive|FDT|FIT|SDIO|GPT)\b",
    8: r"\b(VisionFive|jh7110|FDT|FIT|SDIO|GPT|pyserial|hardware runner)\b",
}

REQUIRED_SPEC_IDS = {
    2: ("kernel/boot",),
    3: ("kernel/memory",),
    4: ("kernel/trap", "interface/kernel-uart", "interface/kernel-plic"),
    5: ("kernel/process", "kernel/syscall", "interface/trap-frame"),
    6: ("kernel/fs", "kernel/virtio"),
    7: ("kernel/pipe", "interface/resource"),
    8: ("xv6-upstream-behavior",),
    9: ("kernel/platform", "kernel/sbi", "kernel/sd", "kernel/gpt",
        "interface/block-device"),
}

REQUIRED_CHECKS = {
    2: ("bootstrap_banner_not_null",),
    3: ("kalloc_alignment", "kvmmake_identity_mapping"),
    4: ("trap_init_stvec_set", "devintr_timer", "uart_boot_output"),
    5: ("fork_returns_different_pid", "syscall_valid_number"),
    6: ("bread_cache_hit", "log_recovery_committed", "exec_valid_elf"),
    7: ("fd_alloc_close_cycle", "pipe_read_write_cycle", "shell_boots"),
    8: ("usertests_all_pass",),
    9: ("vf2_platform_contract", "vf2_image_tools"),
}


def git(*args: str, check: bool = True) -> str:
    result = subprocess.run(["git", *args], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and result.returncode:
        raise RuntimeError(result.stderr.strip())
    return result.stdout


def audit_lab(lab: int, tag: str) -> list[str]:
    errors: list[str] = []
    tag_type = git("cat-file", "-t", tag).strip()
    if tag_type != "tag":
        errors.append(f"{tag}: must be an annotated tag, got {tag_type}")
        return errors
    files = git("ls-tree", "-r", "--name-only", f"{tag}^{{commit}}").splitlines()
    if any(path == ".vos" or path.startswith(".vos/") for path in files):
        errors.append(f"{tag}: tracked .vos evidence")
    for path, introduced in INTRODUCED_PATHS.items():
        if lab >= introduced:
            continue
        leaked = [item for item in files if item == path or
                  (path.endswith("/") and item.startswith(path))]
        if leaked:
            errors.append(f"{tag}: future path {leaked[0]} (Lab {introduced})")
    if lab in FUTURE_TERMS:
        result = subprocess.run(
            ["git", "grep", "-I", "-n", "-E", FUTURE_TERMS[lab],
             f"{tag}^{{commit}}", "--", "*.md", "*.yaml", "*.yml", "*.sh", "*.py"],
            cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode == 0:
            errors.append(f"{tag}: future term: {result.stdout.splitlines()[0]}")
        elif result.returncode != 1:
            errors.append(f"{tag}: git grep failed: {result.stderr.strip()}")
    tree_text = git("grep", "-I", "-h", ".", f"{tag}^{{commit}}", "--",
                    "spec/*.yaml", "spec/**/*.yaml", "vos.yaml", check=False)
    for introduced, ids in REQUIRED_SPEC_IDS.items():
        if introduced <= lab:
            for spec_id in ids:
                if spec_id not in tree_text:
                    errors.append(f"{tag}: missing cumulative Spec ID {spec_id}")
    for introduced, checks in REQUIRED_CHECKS.items():
        if introduced <= lab:
            for check_id in checks:
                if check_id not in tree_text:
                    errors.append(f"{tag}: missing cumulative check ID {check_id}")
    return errors


def main() -> None:
    failures: list[str] = []
    for lab, tag in TAGS.items():
        failures.extend(audit_lab(lab, tag))
    if failures:
        print("course history audit failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        raise SystemExit(1)
    print("course history audit passed for Lab 1-10")


if __name__ == "__main__":
    main()

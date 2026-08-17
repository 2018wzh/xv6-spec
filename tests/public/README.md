# Lab 1 public CTF fixture

This teacher-owned framework generates two non-secret files and a bounded read-only image from one fixed seed. Student implementations may consume the files or image, but must not modify this directory or copy expected values into source.

The image format is intentionally small and fully public:

- bytes `0..7`: `VOSCTF1\0`;
- little-endian header fields at bytes `8..27`: entry count, directory offset, directory-entry size, sector size, and image size;
- two 32-byte directory entries at byte 64; each contains an 8-byte name, 32-bit data offset, 32-bit length, and the first 16 bytes of the content SHA-256;
- file data starts at sector-aligned offsets 512 and 1024;
- the complete image is 4096 bytes.

Generate a fixture with:

```sh
bun tests/public/ctf-fixture.ts generate build/ctf-fixture 7
```

Each reader must emit the ordered redacted records stored in `metadata.json`. Validate one or more logs with:

```sh
bun tests/public/ctf-fixture.ts verify build/ctf-fixture/metadata.json build/linux.log build/baremetal.log
```

The framework checks provenance records only. It does not implement Linux file I/O, image parsing, UART output, a RISC-V entry point, QEMU wiring, or the completion marker; those remain student implementation work.

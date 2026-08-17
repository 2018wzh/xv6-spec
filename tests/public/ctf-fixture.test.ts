import { afterEach, describe, expect, test } from "bun:test";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import {
  CTF_DATA_OFFSETS,
  CTF_DIRECTORY_ENTRY_SIZE,
  CTF_DIRECTORY_OFFSET,
  CTF_IMAGE_SIZE,
  generateCtfFixture,
  verifyCtfRecords,
} from "./ctf-fixture.ts";

const roots: string[] = [];

afterEach(async () => {
  await Promise.all(roots.splice(0).map((root) => rm(root, { recursive: true, force: true })));
});

describe("Lab 1 public CTF fixture", () => {
  test("generates deterministic files and a bounded directory image", async () => {
    const first = await makeRoot();
    const second = await makeRoot();
    const firstMetadata = await generateCtfFixture(first, 7);
    const secondMetadata = await generateCtfFixture(second, 7);

    expect(firstMetadata).toEqual(secondMetadata);
    expect(firstMetadata.image.size).toBe(CTF_IMAGE_SIZE);
    expect(new Uint8Array(await readFile(join(first, "flags.img")))).toEqual(
      new Uint8Array(await readFile(join(second, "flags.img"))),
    );
    expect(firstMetadata.files.map((file) => file.offset)).toEqual([...CTF_DATA_OFFSETS]);
    expect(firstMetadata.files.every((file) => file.record.includes(file.sha256))).toBe(true);

    const image = new Uint8Array(await readFile(join(first, "flags.img")));
    const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
    expect(new TextDecoder().decode(image.subarray(0, 8))).toBe("VOSCTF1\0");
    expect(view.getUint32(8, true)).toBe(2);
    expect(view.getUint32(12, true)).toBe(CTF_DIRECTORY_OFFSET);
    expect(view.getUint32(16, true)).toBe(CTF_DIRECTORY_ENTRY_SIZE);
  });

  test("accepts ordered redacted records and rejects missing or reordered evidence", async () => {
    const root = await makeRoot();
    const fixture = join(root, "fixture");
    const metadata = await generateCtfFixture(fixture, 11);
    const valid = join(root, "valid.log");
    const invalid = join(root, "invalid.log");
    await writeFile(valid, `booting\n${metadata.alternating_records.join("\n")}\nCTF_BAREMETAL_OK\n`);
    await writeFile(invalid, `${[...metadata.alternating_records].reverse().join("\n")}\n`);

    await expect(verifyCtfRecords(join(fixture, "metadata.json"), [valid])).resolves.toBeUndefined();
    await expect(verifyCtfRecords(join(fixture, "metadata.json"), [invalid])).rejects.toThrow(/missing ordered record/);
  });

  test("rejects invalid seeds and malformed metadata", async () => {
    const root = await makeRoot();
    await expect(generateCtfFixture(join(root, "fixture"), -1)).rejects.toThrow(/unsigned 32-bit/);
    const metadata = join(root, "bad.json");
    const output = join(root, "output.log");
    await writeFile(metadata, "{}\n");
    await writeFile(output, "CTF_RECORD\n");
    await expect(verifyCtfRecords(metadata, [output])).rejects.toThrow(/unsupported fixture metadata/);
  });
});

async function makeRoot(): Promise<string> {
  const root = await mkdtemp(join(tmpdir(), "vos-lab1-ctf-"));
  roots.push(root);
  await mkdir(root, { recursive: true });
  return root;
}

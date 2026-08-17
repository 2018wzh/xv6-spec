import { createHash } from "node:crypto";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";

export const CTF_IMAGE_SIZE = 4096;
export const CTF_SECTOR_SIZE = 512;
export const CTF_DIRECTORY_OFFSET = 64;
export const CTF_DIRECTORY_ENTRY_SIZE = 32;
export const CTF_DATA_OFFSETS = [512, 1024] as const;

const CTF_MAGIC = new TextEncoder().encode("VOSCTF1\0");
const FILE_NAMES = ["flag1", "flag2"] as const;

export interface CtfFixtureFile {
  name: typeof FILE_NAMES[number];
  offset: number;
  length: number;
  sha256: string;
  record: string;
}

export interface CtfFixtureMetadata {
  version: "vos.ctf-fixture.v1";
  seed: number;
  image: {
    path: "flags.img";
    size: number;
    sector_size: number;
    sha256: string;
  };
  files: CtfFixtureFile[];
  alternating_records: string[];
}

export async function generateCtfFixture(outputDirectory: string, seed = 0x5eed_0001): Promise<CtfFixtureMetadata> {
  assertSeed(seed);
  const outputRoot = resolve(outputDirectory);
  await mkdir(outputRoot, { recursive: true });

  const contents = FILE_NAMES.map((name, index) =>
    new TextEncoder().encode(`PUBLIC_${name.toUpperCase()}_${deterministicHex(seed, index)}\n`));
  const image = new Uint8Array(CTF_IMAGE_SIZE);
  image.set(CTF_MAGIC, 0);
  const view = new DataView(image.buffer);
  view.setUint32(8, FILE_NAMES.length, true);
  view.setUint32(12, CTF_DIRECTORY_OFFSET, true);
  view.setUint32(16, CTF_DIRECTORY_ENTRY_SIZE, true);
  view.setUint32(20, CTF_SECTOR_SIZE, true);
  view.setUint32(24, CTF_IMAGE_SIZE, true);

  const files = FILE_NAMES.map((name, index): CtfFixtureFile => {
    const content = contents[index]!;
    const offset = CTF_DATA_OFFSETS[index]!;
    if (offset + content.length > CTF_IMAGE_SIZE) {
      throw new Error(`fixture ${name} exceeds the bounded image`);
    }
    const entryOffset = CTF_DIRECTORY_OFFSET + index * CTF_DIRECTORY_ENTRY_SIZE;
    image.set(new TextEncoder().encode(name), entryOffset);
    view.setUint32(entryOffset + 8, offset, true);
    view.setUint32(entryOffset + 12, content.length, true);
    image.set(hexBytes(sha256(content)).subarray(0, 16), entryOffset + 16);
    image.set(content, offset);
    const digest = sha256(content);
    return {
      name,
      offset,
      length: content.length,
      sha256: digest,
      record: recordFor(name, content.length, digest),
    };
  });

  for (let index = 0; index < FILE_NAMES.length; index++) {
    await writeFile(join(outputRoot, FILE_NAMES[index]!), contents[index]!);
  }
  await writeFile(join(outputRoot, "flags.img"), image);
  const metadata: CtfFixtureMetadata = {
    version: "vos.ctf-fixture.v1",
    seed,
    image: {
      path: "flags.img",
      size: image.length,
      sector_size: CTF_SECTOR_SIZE,
      sha256: sha256(image),
    },
    files,
    alternating_records: files.map((file) => file.record),
  };
  await writeFile(join(outputRoot, "metadata.json"), `${JSON.stringify(metadata, null, 2)}\n`);
  return metadata;
}

export async function verifyCtfRecords(metadataPath: string, outputPaths: string[]): Promise<void> {
  if (outputPaths.length === 0) throw new Error("at least one output path is required");
  const metadata = parseMetadata(JSON.parse(await readFile(metadataPath, "utf8")));
  for (const outputPath of outputPaths) {
    const lines = (await readFile(outputPath, "utf8"))
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter(Boolean);
    let cursor = 0;
    for (const expected of metadata.alternating_records) {
      const found = lines.indexOf(expected, cursor);
      if (found < 0) throw new Error(`${outputPath} is missing ordered record ${expected}`);
      cursor = found + 1;
    }
  }
}

function parseMetadata(value: unknown): CtfFixtureMetadata {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("fixture metadata must be an object");
  const metadata = value as Partial<CtfFixtureMetadata>;
  if (metadata.version !== "vos.ctf-fixture.v1" || !Array.isArray(metadata.files) || !Array.isArray(metadata.alternating_records)) {
    throw new Error("unsupported fixture metadata");
  }
  if (metadata.files.length !== 2 || metadata.alternating_records.length !== 2) {
    throw new Error("fixture metadata must describe exactly two alternating records");
  }
  return metadata as CtfFixtureMetadata;
}

function deterministicHex(seed: number, stream: number): string {
  let state = (seed ^ Math.imul(stream + 1, 0x9e37_79b9)) >>> 0;
  let output = "";
  for (let index = 0; index < 4; index++) {
    state ^= state << 13;
    state ^= state >>> 17;
    state ^= state << 5;
    output += (state >>> 0).toString(16).padStart(8, "0");
  }
  return output;
}

function recordFor(name: string, length: number, digest: string): string {
  return `CTF_RECORD name=${name} length=${length} sha256=${digest}`;
}

function assertSeed(seed: number): void {
  if (!Number.isSafeInteger(seed) || seed < 0 || seed > 0xffff_ffff) {
    throw new Error("seed must be an unsigned 32-bit integer");
  }
}

function sha256(value: Uint8Array): string {
  return createHash("sha256").update(value).digest("hex");
}

function hexBytes(value: string): Uint8Array {
  return Uint8Array.from(value.match(/.{2}/g)!.map((pair) => Number.parseInt(pair, 16)));
}

async function main(args: string[]): Promise<void> {
  const [command, ...rest] = args;
  if (command === "generate" && (rest.length === 1 || rest.length === 2)) {
    const seed = rest[1] === undefined ? undefined : Number(rest[1]);
    await generateCtfFixture(rest[0]!, seed);
    return;
  }
  if (command === "verify" && rest.length >= 2) {
    await verifyCtfRecords(rest[0]!, rest.slice(1));
    return;
  }
  throw new Error("usage: bun tests/public/ctf-fixture.ts generate <output-dir> [seed] | verify <metadata.json> <output>...");
}

if (import.meta.main) {
  await main(process.argv.slice(2));
}

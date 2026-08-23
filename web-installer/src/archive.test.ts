import { describe, expect, it } from "vitest";
import { ArchiveError, extractUpdateBundle, parseTar } from "./archive";

const encoder = new TextEncoder();

function writeField(header: Uint8Array, offset: number, length: number, value: string): void {
  const encoded = encoder.encode(value);
  header.set(encoded.subarray(0, length), offset);
}

function tar(entries: readonly { path: string; data?: Uint8Array; type?: "file" | "directory" }[]): Uint8Array {
  const blocks: Uint8Array[] = [];
  for (const entry of entries) {
    const data = entry.data ?? new Uint8Array();
    const header = new Uint8Array(512);
    writeField(header, 0, 100, entry.path);
    writeField(header, 100, 8, "0000644\0");
    writeField(header, 108, 8, "0000000\0");
    writeField(header, 116, 8, "0000000\0");
    writeField(header, 124, 12, `${data.byteLength.toString(8).padStart(11, "0")}\0`);
    writeField(header, 136, 12, "00000000000\0");
    header.fill(0x20, 148, 156);
    header[156] = entry.type === "directory" ? 0x35 : 0x30;
    writeField(header, 257, 6, "ustar\0");
    writeField(header, 263, 2, "00");
    const checksum = header.reduce((sum, value) => sum + value, 0);
    writeField(header, 148, 8, `${checksum.toString(8).padStart(6, "0")}\0 `);
    blocks.push(header);
    if (data.byteLength) {
      const padded = new Uint8Array(Math.ceil(data.byteLength / 512) * 512);
      padded.set(data);
      blocks.push(padded);
    }
  }
  blocks.push(new Uint8Array(1024));
  const size = blocks.reduce((total, block) => total + block.byteLength, 0);
  const output = new Uint8Array(size);
  let offset = 0;
  for (const block of blocks) {
    output.set(block, offset);
    offset += block.byteLength;
  }
  return output;
}

async function gzip(data: Uint8Array): Promise<Uint8Array> {
  const source = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) as ArrayBuffer;
  const stream = new Blob([source]).stream().pipeThrough(new CompressionStream("gzip"));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

function updateManifest(target = "7", firmware = "firmware.dfu"): Uint8Array {
  return encoder.encode([
    "Filetype: Flipper firmware upgrade configuration",
    "Version: 2",
    "Info: poisonedos-1.2.3",
    `Target: ${target}`,
    "Loader: updater.bin",
    `Firmware: ${firmware}`,
    "Resources: resources.ths",
    "",
  ].join("\n"));
}

function validTar(target = "7", firmware = "firmware.dfu"): Uint8Array {
  return tar([
    { path: "f7-update-poisonedos/", type: "directory" },
    { path: "f7-update-poisonedos/update.fuf", data: updateManifest(target, firmware) },
    { path: "f7-update-poisonedos/updater.bin", data: Uint8Array.from([1, 2, 3]) },
    { path: "f7-update-poisonedos/firmware.dfu", data: Uint8Array.from([4, 5, 6]) },
    { path: "f7-update-poisonedos/resources.ths", data: Uint8Array.from([7, 8, 9]) },
  ]);
}

describe("Poisoned_Os update archive", () => {
  it("extracts a complete target-7 update and records every file digest", async () => {
    const bundle = await extractUpdateBundle(await gzip(validTar()));
    expect(bundle).toMatchObject({
      directory: "f7-update-poisonedos",
      manifestPath: "f7-update-poisonedos/update.fuf",
      versionLabel: "poisonedos-1.2.3",
      totalBytes: updateManifest().byteLength + 9,
    });
    expect(bundle.archiveSha256).toMatch(/^[0-9a-f]{64}$/);
    expect(bundle.files).toHaveLength(4);
    expect(bundle.files.every((file) => /^[0-9a-f]{64}$/.test(file.sha256))).toBe(true);
  });

  it("rejects path traversal before exposing an entry", () => {
    expect(() => parseTar(tar([{ path: "../firmware.dfu", data: Uint8Array.from([1]) }])))
      .toThrowError(/Unsafe path/);
  });

  it("rejects a damaged tar header", () => {
    const damaged = validTar();
    damaged[10] = damaged[10]! ^ 0xff;
    expect(() => parseTar(damaged)).toThrowError(/damaged tar header/);
  });

  it("rejects duplicate normalized tar paths", () => {
    expect(() => parseTar(tar([
      { path: "f7-update-poisonedos/update.fuf", data: updateManifest() },
      { path: "f7-update-poisonedos/update.fuf", data: updateManifest() },
    ]))).toThrowError(/Duplicate path/);
  });

  it("rejects non-padding bytes after the tar terminator", () => {
    const archive = validTar();
    const trailing = new Uint8Array(archive.byteLength + 1);
    trailing.set(archive);
    trailing[archive.byteLength] = 1;
    expect(() => parseTar(trailing)).toThrowError(/data after its tar terminator/);
  });

  it("rejects a package for any hardware target other than 7", async () => {
    await expect(extractUpdateBundle(await gzip(validTar("8"))))
      .rejects.toThrowError(/not for Flipper Zero hardware target 7/);
  });

  it("rejects update.fuf references to files absent from the archive", async () => {
    await expect(extractUpdateBundle(await gzip(validTar("7", "missing.dfu"))))
      .rejects.toThrowError(/missing firmware/);
  });

  it("rejects bytes that are not gzip without returning a partial bundle", async () => {
    await expect(extractUpdateBundle(Uint8Array.from([1, 2, 3])))
      .rejects.toBeInstanceOf(ArchiveError);
  });

  it("stops decompression when expanded data exceeds the 64 MiB limit", async () => {
    const compressed = await gzip(new Uint8Array(64 * 1024 * 1024 + 1));
    await expect(extractUpdateBundle(compressed)).rejects.toThrowError(/expanded update archive is too large/);
  });
});

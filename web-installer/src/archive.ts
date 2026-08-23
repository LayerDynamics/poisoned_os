const TAR_BLOCK_BYTES = 512;
const MAX_ARCHIVE_BYTES = 32 * 1024 * 1024;
const MAX_EXPANDED_BYTES = 64 * 1024 * 1024;
const MAX_ENTRIES = 96;
const utf8 = new TextDecoder("utf-8", { fatal: true });

export interface UpdateFile {
  readonly path: string;
  readonly data: Uint8Array;
  readonly sha256: string;
}

export interface UpdateBundle {
  readonly archiveSha256: string;
  readonly directory: string;
  readonly files: readonly UpdateFile[];
  readonly manifestPath: string;
  readonly totalBytes: number;
  readonly versionLabel: string;
}

export class ArchiveError extends Error {
  public constructor(message: string) {
    super(message);
    this.name = "ArchiveError";
  }
}

function textField(block: Uint8Array, start: number, length: number): string {
  const field = block.subarray(start, start + length);
  const end = field.indexOf(0);
  try {
    return utf8.decode(end === -1 ? field : field.subarray(0, end)).trim();
  } catch {
    throw new ArchiveError("The update archive contains a non-UTF-8 tar path");
  }
}

function octalField(block: Uint8Array, start: number, length: number, label: string): number {
  const value = textField(block, start, length).replaceAll("\0", "").trim();
  if (!/^[0-7]+$/.test(value)) throw new ArchiveError(`Invalid tar ${label}`);
  const parsed = Number.parseInt(value, 8);
  if (!Number.isSafeInteger(parsed) || parsed < 0) throw new ArchiveError(`Invalid tar ${label}`);
  return parsed;
}

function isZeroBlock(block: Uint8Array): boolean {
  return block.every((value) => value === 0);
}

function verifyHeaderChecksum(header: Uint8Array): void {
  const expected = octalField(header, 148, 8, "header checksum");
  let actual = 0;
  for (let index = 0; index < header.byteLength; index += 1) {
    actual += index >= 148 && index < 156 ? 0x20 : header[index]!;
  }
  if (actual !== expected) throw new ArchiveError("The update archive has a damaged tar header");
}

function safeArchivePath(path: string): string {
  if (!path || path.startsWith("/") || path.includes("\\") || path.includes("\0")) {
    throw new ArchiveError(`Unsafe path in update archive: ${path || "<empty>"}`);
  }
  const parts = path.replace(/\/$/, "").split("/");
  if (parts.some((part) => !part || part === "." || part === "..")) {
    throw new ArchiveError(`Unsafe path in update archive: ${path}`);
  }
  return parts.join("/");
}

export interface TarEntry {
  readonly path: string;
  readonly type: "file" | "directory";
  readonly data: Uint8Array;
}

export function parseTar(bytes: Uint8Array): readonly TarEntry[] {
  if (bytes.byteLength > MAX_EXPANDED_BYTES) throw new ArchiveError("The expanded update archive is too large");
  const entries: TarEntry[] = [];
  const paths = new Set<string>();
  let offset = 0;
  let zeroBlocks = 0;
  while (offset + TAR_BLOCK_BYTES <= bytes.byteLength) {
    const header = bytes.subarray(offset, offset + TAR_BLOCK_BYTES);
    offset += TAR_BLOCK_BYTES;
    if (isZeroBlock(header)) {
      zeroBlocks += 1;
      if (zeroBlocks === 2) break;
      continue;
    }
    if (zeroBlocks !== 0) throw new ArchiveError("The update archive has data after its tar terminator");
    verifyHeaderChecksum(header);
    const name = textField(header, 0, 100);
    const prefix = textField(header, 345, 155);
    const path = safeArchivePath(prefix ? `${prefix}/${name}` : name);
    if (paths.has(path)) throw new ArchiveError(`Duplicate path in update archive: ${path}`);
    paths.add(path);
    const size = octalField(header, 124, 12, "entry size");
    const typeFlag = header[156] ?? 0;
    const type = typeFlag === 0 || typeFlag === 0x30
      ? "file"
      : typeFlag === 0x35
        ? "directory"
        : null;
    if (!type) throw new ArchiveError(`Unsupported tar entry type for ${path}`);
    if (type === "directory" && size !== 0) throw new ArchiveError(`Directory ${path} contains unexpected data`);
    if (offset + size > bytes.byteLength) throw new ArchiveError(`Truncated tar entry: ${path}`);
    const data = Uint8Array.from(bytes.subarray(offset, offset + size));
    const paddedSize = Math.ceil(size / TAR_BLOCK_BYTES) * TAR_BLOCK_BYTES;
    offset += paddedSize;
    if (offset > bytes.byteLength) throw new ArchiveError(`Truncated tar padding after ${path}`);
    entries.push({ path, type, data });
    if (entries.length > MAX_ENTRIES) throw new ArchiveError("The update archive contains too many entries");
  }
  if (zeroBlocks < 2) throw new ArchiveError("The update archive has no complete tar terminator");
  if (bytes.subarray(offset).some((value) => value !== 0)) {
    throw new ArchiveError("The update archive has data after its tar terminator");
  }
  return entries;
}

async function gunzip(bytes: Uint8Array): Promise<Uint8Array> {
  if (typeof DecompressionStream === "undefined") {
    throw new ArchiveError("This browser cannot decompress firmware packages");
  }
  try {
    const source = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer;
    const stream = new Blob([source]).stream().pipeThrough(new DecompressionStream("gzip"));
    const reader = stream.getReader();
    const chunks: Uint8Array[] = [];
    let total = 0;
    try {
      while (true) {
        const result = await reader.read();
        if (result.done) break;
        total += result.value.byteLength;
        if (total > MAX_EXPANDED_BYTES) {
          await reader.cancel();
          throw new ArchiveError("The expanded update archive is too large");
        }
        chunks.push(Uint8Array.from(result.value));
      }
    } finally {
      reader.releaseLock();
    }
    const expanded = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) {
      expanded.set(chunk, offset);
      offset += chunk.byteLength;
    }
    return expanded;
  } catch (error) {
    if (error instanceof ArchiveError) throw error;
    throw new ArchiveError(`The firmware package is not valid gzip data: ${error instanceof Error ? error.message : String(error)}`);
  }
}

export async function sha256(bytes: Uint8Array): Promise<string> {
  const source = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer;
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", source));
  return Array.from(digest, (value) => value.toString(16).padStart(2, "0")).join("");
}

function parseManifest(data: Uint8Array): Map<string, string> {
  let content: string;
  try {
    content = utf8.decode(data);
  } catch {
    throw new ArchiveError("update.fuf is not UTF-8 text");
  }
  const fields = new Map<string, string>();
  for (const rawLine of content.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#")) continue;
    const separator = line.indexOf(":");
    if (separator <= 0) throw new ArchiveError(`Invalid update.fuf line: ${line}`);
    const key = line.slice(0, separator).trim();
    if (fields.has(key)) throw new ArchiveError(`Duplicate update.fuf field: ${key}`);
    fields.set(key, line.slice(separator + 1).trim());
  }
  return fields;
}

function validateBundle(entries: readonly TarEntry[]): { directory: string; manifestPath: string; files: TarEntry[]; versionLabel: string } {
  if (entries.length === 0) throw new ArchiveError("The update archive is empty");
  const roots = new Set(entries.map((entry) => entry.path.split("/")[0]));
  if (roots.size !== 1) throw new ArchiveError("Every update file must be inside one package directory");
  const directory = [...roots][0]!;
  const files = entries.filter((entry) => entry.type === "file");
  const relative = new Map<string, TarEntry>();
  for (const file of files) {
    if (!file.path.startsWith(`${directory}/`)) throw new ArchiveError(`Update file escaped package directory: ${file.path}`);
    const path = file.path.slice(directory.length + 1);
    if (relative.has(path)) throw new ArchiveError(`Duplicate update file: ${path}`);
    relative.set(path, file);
  }
  const manifest = relative.get("update.fuf");
  if (!manifest) throw new ArchiveError("The update package does not contain update.fuf");
  const fields = parseManifest(manifest.data);
  if (fields.get("Filetype") !== "Flipper firmware upgrade configuration") {
    throw new ArchiveError("update.fuf is not a Flipper firmware upgrade manifest");
  }
  if (fields.get("Version") !== "2") throw new ArchiveError("Only update manifest version 2 is supported");
  if (fields.get("Target") !== "7") throw new ArchiveError("This package is not for Flipper Zero hardware target 7");
  for (const field of ["Loader", "Firmware"] as const) {
    const value = fields.get(field);
    if (!value || !relative.has(value)) throw new ArchiveError(`update.fuf references a missing ${field.toLowerCase()}`);
  }
  for (const field of ["Radio", "Resources", "Splashscreen"] as const) {
    const value = fields.get(field);
    if (value && !relative.has(value)) throw new ArchiveError(`update.fuf references missing file ${value}`);
  }
  return {
    directory,
    manifestPath: `${directory}/update.fuf`,
    files,
    versionLabel: fields.get("Info") || directory,
  };
}

export async function extractUpdateBundle(archive: Uint8Array): Promise<UpdateBundle> {
  if (archive.byteLength === 0) throw new ArchiveError("The firmware package is empty");
  if (archive.byteLength > MAX_ARCHIVE_BYTES) throw new ArchiveError("The firmware package is too large");
  const archiveSha256 = await sha256(archive);
  const parsed = validateBundle(parseTar(await gunzip(archive)));
  const files: UpdateFile[] = [];
  for (const entry of parsed.files) {
    files.push({ path: entry.path, data: entry.data, sha256: await sha256(entry.data) });
  }
  return {
    archiveSha256,
    directory: parsed.directory,
    files,
    manifestPath: parsed.manifestPath,
    totalBytes: files.reduce((total, file) => total + file.data.byteLength, 0),
    versionLabel: parsed.versionLabel,
  };
}

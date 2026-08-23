import { sha256 } from "./archive";

const FEED_SCHEMA = "poison.web-installer-feed/v1";
const RELEASE_SCHEMA = "poison.release-manifest/v1";
const SIGNATURE_ALGORITHM = "ECDSA-P256-SHA256";
const DIGEST = /^[0-9a-f]{64}$/;
const VERSION = /^\d+\.\d+\.\d+$/;
const IDENTIFIER = /^[a-z0-9][a-z0-9._-]{0,63}$/;
const MAX_FEED_BYTES = 512 * 1024;
const MAX_PACKAGE_BYTES = 32 * 1024 * 1024;
const MAX_COMPONENT_BYTES = Number.MAX_SAFE_INTEGER;

export type ReleaseChannel = "stable" | "beta" | "developer" | "internal";

export interface ReleaseComponent {
  readonly id: string;
  readonly path: string;
  readonly sha256: string;
  readonly bytes: number;
}

export interface ReleaseSignature {
  readonly algorithm: typeof SIGNATURE_ALGORITHM;
  readonly keyId: string;
  readonly value: string;
}

export interface SignedReleaseManifest {
  readonly schema: typeof RELEASE_SCHEMA;
  readonly version: string;
  readonly channel: ReleaseChannel;
  readonly target: string;
  readonly rollbackVersion: string;
  readonly components: readonly ReleaseComponent[];
  readonly revocations: readonly string[];
  readonly signature: ReleaseSignature;
  readonly minimumVersion?: string;
  readonly maximumVersion?: string;
}

export interface InstallerRelease {
  readonly manifest: SignedReleaseManifest;
  readonly packageComponentId: string;
  readonly packageUrl: string;
  readonly resolvedPackageUrl: string;
  readonly component: ReleaseComponent;
}

export interface InstallerFeed {
  readonly schema: typeof FEED_SCHEMA;
  readonly releases: readonly InstallerRelease[];
  readonly sourceUrl: string;
}

export type TrustedReleaseKeys = Readonly<Record<string, string>>;

export class ReleaseFeedError extends Error {
  public constructor(message: string) {
    super(message);
    this.name = "ReleaseFeedError";
  }
}

function object(value: unknown, label: string): Record<string, unknown> {
  if (typeof value !== "object" || value === null || Array.isArray(value)) throw new ReleaseFeedError(`${label} must be an object`);
  return value as Record<string, unknown>;
}

function exactKeys(value: Record<string, unknown>, allowed: readonly string[], label: string): void {
  const unknown = Object.keys(value).filter((key) => !allowed.includes(key));
  if (unknown.length) throw new ReleaseFeedError(`${label} contains unsupported field ${unknown[0]}`);
}

function component(value: unknown): ReleaseComponent {
  const item = object(value, "release component");
  exactKeys(item, ["id", "path", "sha256", "bytes"], "release component");
  if (typeof item.id !== "string" || !IDENTIFIER.test(item.id)) throw new ReleaseFeedError("Release component id is invalid");
  if (typeof item.path !== "string" || item.path.startsWith("/") || item.path.includes("\\") ||
      item.path.split("/").some((part) => !part || part === "." || part === "..")) {
    throw new ReleaseFeedError(`Release component path is unsafe: ${String(item.path)}`);
  }
  if (typeof item.sha256 !== "string" || !DIGEST.test(item.sha256)) throw new ReleaseFeedError(`Release component ${item.id} has an invalid digest`);
  if (!Number.isSafeInteger(item.bytes) || (item.bytes as number) < 1 || (item.bytes as number) > MAX_COMPONENT_BYTES) {
    throw new ReleaseFeedError(`Release component ${item.id} has an invalid size`);
  }
  return { id: item.id, path: item.path, sha256: item.sha256, bytes: item.bytes as number };
}

export function validateSignedManifest(value: unknown): SignedReleaseManifest {
  const manifest = object(value, "release manifest");
  exactKeys(manifest, [
    "schema", "version", "channel", "target", "rollbackVersion", "minimumVersion", "maximumVersion",
    "components", "revocations", "signature",
  ], "release manifest");
  if (manifest.schema !== RELEASE_SCHEMA) throw new ReleaseFeedError("Release manifest schema is invalid");
  if (typeof manifest.version !== "string" || !VERSION.test(manifest.version)) throw new ReleaseFeedError("Release version is invalid");
  if (typeof manifest.rollbackVersion !== "string" || !VERSION.test(manifest.rollbackVersion)) throw new ReleaseFeedError("Rollback version is invalid");
  if (!["stable", "beta", "developer", "internal"].includes(String(manifest.channel))) throw new ReleaseFeedError("Release channel is invalid");
  if (manifest.target !== "f7" && manifest.target !== "7") throw new ReleaseFeedError("Release is not for Flipper Zero target 7");
  for (const bound of ["minimumVersion", "maximumVersion"] as const) {
    if (manifest[bound] !== undefined && (typeof manifest[bound] !== "string" || !VERSION.test(manifest[bound] as string))) {
      throw new ReleaseFeedError(`${bound} is invalid`);
    }
  }
  if (!Array.isArray(manifest.components) || manifest.components.length === 0 || manifest.components.length > 64) {
    throw new ReleaseFeedError("Release components must be a non-empty bounded array");
  }
  const components = manifest.components.map(component);
  if (new Set(components.map((item) => item.id)).size !== components.length) throw new ReleaseFeedError("Release component ids must be unique");
  if (!Array.isArray(manifest.revocations) || manifest.revocations.length > 64 ||
      manifest.revocations.some((digest) => typeof digest !== "string" || !DIGEST.test(digest)) ||
      new Set(manifest.revocations).size !== manifest.revocations.length) {
    throw new ReleaseFeedError("Release revocations are invalid");
  }
  const signatureValue = object(manifest.signature, "release signature");
  exactKeys(signatureValue, ["algorithm", "keyId", "value"], "release signature");
  if (signatureValue.algorithm !== SIGNATURE_ALGORITHM || typeof signatureValue.keyId !== "string" ||
      !IDENTIFIER.test(signatureValue.keyId) || typeof signatureValue.value !== "string") {
    throw new ReleaseFeedError("Release signature metadata is invalid");
  }
  try { base64Bytes(signatureValue.value); } catch { throw new ReleaseFeedError("Release signature is not valid base64"); }
  return {
    schema: RELEASE_SCHEMA,
    version: manifest.version,
    channel: manifest.channel as ReleaseChannel,
    target: manifest.target,
    rollbackVersion: manifest.rollbackVersion,
    components,
    revocations: manifest.revocations as string[],
    signature: {
      algorithm: SIGNATURE_ALGORITHM,
      keyId: signatureValue.keyId,
      value: signatureValue.value,
    },
    ...(manifest.minimumVersion === undefined ? {} : { minimumVersion: manifest.minimumVersion as string }),
    ...(manifest.maximumVersion === undefined ? {} : { maximumVersion: manifest.maximumVersion as string }),
  };
}

function safeHttpUrl(value: string, base: string): string {
  const url = new URL(value, base);
  const local = url.hostname === "127.0.0.1" || url.hostname === "localhost" || url.hostname === "[::1]";
  if (url.protocol !== "https:" && !(url.protocol === "http:" && local)) {
    throw new ReleaseFeedError("Firmware packages must use HTTPS or a loopback development URL");
  }
  if (url.username || url.password) throw new ReleaseFeedError("Firmware package URLs cannot contain credentials");
  return url.href;
}

export function validateFeed(value: unknown, sourceUrl: string): InstallerFeed {
  const feed = object(value, "installer feed");
  exactKeys(feed, ["schema", "releases"], "installer feed");
  if (feed.schema !== FEED_SCHEMA) throw new ReleaseFeedError("Installer feed schema is invalid");
  if (!Array.isArray(feed.releases) || feed.releases.length === 0 || feed.releases.length > 32) {
    throw new ReleaseFeedError("Installer feed must contain 1 to 32 releases");
  }
  const releases = feed.releases.map((value, index): InstallerRelease => {
    const release = object(value, `installer release ${index + 1}`);
    exactKeys(release, ["manifest", "packageComponentId", "packageUrl"], `installer release ${index + 1}`);
    const manifest = validateSignedManifest(release.manifest);
    if (typeof release.packageComponentId !== "string" || !IDENTIFIER.test(release.packageComponentId)) {
      throw new ReleaseFeedError("Installer package component id is invalid");
    }
    if (typeof release.packageUrl !== "string") throw new ReleaseFeedError("Installer package URL is invalid");
    const selected = manifest.components.find((item) => item.id === release.packageComponentId);
    if (!selected) throw new ReleaseFeedError(`Release ${manifest.version} does not contain its installer package component`);
    if (!selected.path.endsWith(".tgz")) throw new ReleaseFeedError(`Release ${manifest.version} installer component is not a .tgz package`);
    if (selected.bytes > MAX_PACKAGE_BYTES) throw new ReleaseFeedError(`Release ${manifest.version} installer package exceeds the browser size limit`);
    return {
      manifest,
      packageComponentId: release.packageComponentId,
      packageUrl: release.packageUrl,
      resolvedPackageUrl: safeHttpUrl(release.packageUrl, sourceUrl),
      component: selected,
    };
  });
  const identities = releases.map((release) => `${release.manifest.channel}:${release.manifest.version}`);
  if (new Set(identities).size !== identities.length) throw new ReleaseFeedError("Installer feed contains a duplicate release");
  return { schema: FEED_SCHEMA, releases, sourceUrl };
}

function canonicalize(value: unknown): string {
  if (value === null || typeof value !== "object") return JSON.stringify(value);
  if (Array.isArray(value)) return `[${value.map(canonicalize).join(",")}]`;
  const record = value as Record<string, unknown>;
  return `{${Object.keys(record).sort().map((key) => `${JSON.stringify(key)}:${canonicalize(record[key])}`).join(",")}}`;
}

export function signedManifestPayload(manifest: SignedReleaseManifest): Uint8Array {
  const { signature: _signature, ...unsigned } = manifest;
  return new TextEncoder().encode(`${canonicalize(unsigned)}\n`);
}

function base64Bytes(value: string): Uint8Array {
  const binary = atob(value);
  return Uint8Array.from(binary, (character) => character.charCodeAt(0));
}

function pemBytes(pem: string): Uint8Array {
  const match = /^-----BEGIN PUBLIC KEY-----\s+([A-Za-z0-9+/=\s]+)-----END PUBLIC KEY-----\s*$/.exec(pem.trim());
  if (!match) throw new ReleaseFeedError("Trusted release key is not an SPKI public key PEM");
  return base64Bytes(match[1]!.replaceAll(/\s/g, ""));
}

function derLength(bytes: Uint8Array, offset: number): { length: number; next: number } {
  const first = bytes[offset];
  if (first === undefined) throw new ReleaseFeedError("Release signature DER is truncated");
  if ((first & 0x80) === 0) return { length: first, next: offset + 1 };
  const count = first & 0x7f;
  if (count < 1 || count > 2 || offset + count >= bytes.byteLength) throw new ReleaseFeedError("Release signature DER length is invalid");
  let length = 0;
  for (let index = 0; index < count; index += 1) length = (length << 8) | bytes[offset + 1 + index]!;
  return { length, next: offset + 1 + count };
}

export function derEcdsaToP1363(der: Uint8Array): Uint8Array {
  if (der[0] !== 0x30) throw new ReleaseFeedError("Release signature is not an ECDSA DER sequence");
  const sequence = derLength(der, 1);
  if (sequence.next + sequence.length !== der.byteLength) throw new ReleaseFeedError("Release signature DER sequence length is invalid");
  let offset = sequence.next;
  const values: Uint8Array[] = [];
  for (let part = 0; part < 2; part += 1) {
    if (der[offset] !== 0x02) throw new ReleaseFeedError("Release signature DER integer is missing");
    const integer = derLength(der, offset + 1);
    offset = integer.next;
    let value = der.subarray(offset, offset + integer.length);
    offset += integer.length;
    const signPadded = value.byteLength > 1 && value[0] === 0;
    if (signPadded) {
      if ((value[1]! & 0x80) === 0) throw new ReleaseFeedError("Release signature DER integer has redundant padding");
      value = value.subarray(1);
    }
    if (value.byteLength === 0 || value.byteLength > 32 || (!signPadded && (value[0]! & 0x80) !== 0)) {
      throw new ReleaseFeedError("Release signature DER integer is invalid");
    }
    const fixed = new Uint8Array(32);
    fixed.set(value, 32 - value.byteLength);
    values.push(fixed);
  }
  if (offset !== der.byteLength) throw new ReleaseFeedError("Release signature DER contains trailing data");
  const raw = new Uint8Array(64);
  raw.set(values[0]!);
  raw.set(values[1]!, 32);
  return raw;
}

export async function verifyReleaseSignature(
  manifest: SignedReleaseManifest,
  trustedKeys: TrustedReleaseKeys,
): Promise<void> {
  const pem = trustedKeys[manifest.signature.keyId];
  if (!pem) throw new ReleaseFeedError(`Release signer ${manifest.signature.keyId} is not trusted by this installer build`);
  let key: CryptoKey;
  try {
    const spki = pemBytes(pem);
    key = await crypto.subtle.importKey(
      "spki",
      spki.buffer.slice(spki.byteOffset, spki.byteOffset + spki.byteLength) as ArrayBuffer,
      { name: "ECDSA", namedCurve: "P-256" },
      false,
      ["verify"],
    );
  } catch (error) {
    if (error instanceof ReleaseFeedError) throw error;
    throw new ReleaseFeedError(`Trusted release key could not be imported: ${error instanceof Error ? error.message : String(error)}`);
  }
  const signature = derEcdsaToP1363(base64Bytes(manifest.signature.value));
  const signatureBytes = signature.buffer.slice(
    signature.byteOffset,
    signature.byteOffset + signature.byteLength,
  ) as ArrayBuffer;
  const payload = signedManifestPayload(manifest);
  const payloadBytes = payload.buffer.slice(payload.byteOffset, payload.byteOffset + payload.byteLength) as ArrayBuffer;
  const valid = await crypto.subtle.verify(
    { name: "ECDSA", hash: "SHA-256" },
    key,
    signatureBytes,
    payloadBytes,
  );
  if (!valid) throw new ReleaseFeedError(`Release ${manifest.version} has an invalid signature`);
}

export function configuredTrustedKeys(raw = import.meta.env.VITE_POISON_RELEASE_KEYS): TrustedReleaseKeys {
  if (!raw) return {};
  let parsed: unknown;
  try { parsed = JSON.parse(raw); } catch { throw new ReleaseFeedError("VITE_POISON_RELEASE_KEYS is not valid JSON"); }
  const keys = object(parsed, "trusted release keys");
  for (const [keyId, pem] of Object.entries(keys)) {
    if (!IDENTIFIER.test(keyId) || typeof pem !== "string") throw new ReleaseFeedError("Trusted release key map is invalid");
    pemBytes(pem);
  }
  return keys as Record<string, string>;
}

function declaredContentLength(response: Response, label: string): number | null {
  const raw = response.headers.get("content-length");
  if (raw === null) return null;
  if (!/^\d+$/.test(raw)) throw new ReleaseFeedError(`${label} has an invalid Content-Length`);
  const bytes = Number(raw);
  if (!Number.isSafeInteger(bytes)) throw new ReleaseFeedError(`${label} has an invalid Content-Length`);
  return bytes;
}

async function readBoundedBody(response: Response, maximumBytes: number, label: string): Promise<Uint8Array> {
  const declared = declaredContentLength(response, label);
  if (declared !== null && declared > maximumBytes) throw new ReleaseFeedError(`${label} is too large`);
  if (!response.body) throw new ReleaseFeedError(`${label} did not provide a response body`);
  const reader = response.body.getReader();
  const chunks: Uint8Array[] = [];
  let total = 0;
  try {
    while (true) {
      const result = await reader.read();
      if (result.done) break;
      total += result.value.byteLength;
      if (total > maximumBytes) {
        await reader.cancel().catch(() => undefined);
        throw new ReleaseFeedError(`${label} is too large`);
      }
      chunks.push(Uint8Array.from(result.value));
    }
  } finally {
    reader.releaseLock();
  }
  const bytes = new Uint8Array(total);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return bytes;
}

export async function loadReleaseFeed(
  url: string,
  trustedKeys: TrustedReleaseKeys,
  fetcher: typeof fetch = fetch,
): Promise<InstallerFeed> {
  const response = await fetcher(url, { cache: "no-store", credentials: "omit", redirect: "error" });
  if (!response.ok) throw new ReleaseFeedError(`Release feed request failed with HTTP ${response.status}`);
  const body = await readBoundedBody(response, MAX_FEED_BYTES, "Release feed");
  let text: string;
  try { text = new TextDecoder("utf-8", { fatal: true }).decode(body); } catch { throw new ReleaseFeedError("Release feed is not UTF-8"); }
  let decoded: unknown;
  try { decoded = JSON.parse(text); } catch { throw new ReleaseFeedError("Release feed is not valid JSON"); }
  const feed = validateFeed(decoded, response.url || new URL(url, location.href).href);
  for (const release of feed.releases) await verifyReleaseSignature(release.manifest, trustedKeys);
  return feed;
}

export async function downloadRelease(
  release: InstallerRelease,
  onProgress?: (received: number, total: number) => void,
  fetcher: typeof fetch = fetch,
): Promise<Uint8Array> {
  const response = await fetcher(release.resolvedPackageUrl, { cache: "no-store", credentials: "omit", redirect: "error" });
  if (!response.ok) throw new ReleaseFeedError(`Firmware download failed with HTTP ${response.status}`);
  const contentLength = declaredContentLength(response, "Firmware download");
  if (contentLength !== null && contentLength !== release.component.bytes) throw new ReleaseFeedError("Firmware download size does not match the signed manifest");
  if (!response.body) throw new ReleaseFeedError("Firmware download did not provide a response body");
  const reader = response.body.getReader();
  const data = new Uint8Array(release.component.bytes);
  let offset = 0;
  while (true) {
    const result = await reader.read();
    if (result.done) break;
    if (offset + result.value.byteLength > data.byteLength) {
      await reader.cancel();
      throw new ReleaseFeedError("Firmware download exceeded the signed size");
    }
    data.set(result.value, offset);
    offset += result.value.byteLength;
    onProgress?.(offset, data.byteLength);
  }
  if (offset !== data.byteLength) throw new ReleaseFeedError("Firmware download ended before the signed size was received");
  const digest = await sha256(data);
  if (digest !== release.component.sha256) throw new ReleaseFeedError("Firmware download SHA-256 does not match the signed release manifest");
  return data;
}

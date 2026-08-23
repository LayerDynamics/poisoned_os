import { strictCspPolicy } from "./csp";

export const MAX_BUNDLE_BYTES = 4 * 1024 * 1024;
export const MAX_ASSETS = 32;

export interface ServedBundleAsset { path: string; sha256: string; size: number; }
export interface ServedBundleMetadata { id: string; version: string; apiVersion: number; entrypoint: string; contentSha256: string; size: number; requestedCapabilities: readonly string[]; assets: readonly ServedBundleAsset[]; }
export interface ServedBundlePayload { metadata: ServedBundleMetadata; files: Readonly<Record<string, Uint8Array>>; }

const DIGEST = /^[0-9a-f]{64}$/;
function safePath(path: string): boolean {
  if (!path || path.length > 256 || path.startsWith("/") || path.includes("\\")) return false;
  return path.split("/").every((segment) => segment.length > 0 && segment !== "." && segment !== ".." &&
    ![...segment].some((value) => value.charCodeAt(0) < 0x20 || value.charCodeAt(0) === 0x7f));
}

export async function sha256(bytes: Uint8Array): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", bytes as BufferSource);
  return [...new Uint8Array(digest)].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

export async function verifyServedBundle(bundle: ServedBundlePayload): Promise<void> {
  const { metadata, files } = bundle;
  if (!metadata.id || metadata.id.length > 64 || !metadata.version || metadata.version.length > 32 || metadata.apiVersion < 1 ||
      !safePath(metadata.entrypoint) || !DIGEST.test(metadata.contentSha256) || metadata.size < 1 || metadata.size > MAX_BUNDLE_BYTES ||
      metadata.assets.length < 1 || metadata.assets.length > MAX_ASSETS || metadata.requestedCapabilities.length > 16 ||
      new Set(metadata.requestedCapabilities).size !== metadata.requestedCapabilities.length ||
      metadata.requestedCapabilities.some((capability) => !capability || capability.length > 64)) throw new Error("invalid served bundle metadata");
  const names = new Set<string>();
  let total = 0;
  for (const asset of metadata.assets) {
    if (!safePath(asset.path) || names.has(asset.path) || !DIGEST.test(asset.sha256) || !Number.isSafeInteger(asset.size) || asset.size < 0) throw new Error("invalid served bundle asset");
    names.add(asset.path);
    const content = files[asset.path];
    if (!content || content.byteLength !== asset.size || await sha256(content) !== asset.sha256) throw new Error("served bundle asset digest mismatch");
    total += content.byteLength;
  }
  if (!names.has(metadata.entrypoint) || total !== metadata.size) throw new Error("served bundle inventory mismatch");
  const ordered = [...metadata.assets].sort((left, right) => left.path < right.path ? -1 : left.path > right.path ? 1 : 0);
  const encoder = new TextEncoder();
  const chunks = ordered.flatMap((asset) => [encoder.encode(asset.path), new Uint8Array([0]), files[asset.path]]);
  const content = new Uint8Array(chunks.reduce((sum, chunk) => sum + chunk.byteLength, 0));
  let offset = 0;
  for (const chunk of chunks) { content.set(chunk, offset); offset += chunk.byteLength; }
  if (await sha256(content) !== metadata.contentSha256) throw new Error("served bundle content digest mismatch");
}

export function strictCsp(): string {
  return strictCspPolicy();
}

export async function materializeServedBundle(
  bundle: ServedBundlePayload,
  options?: { readonly brokerNonce?: string },
): Promise<{ url: string; revoke: () => void }> {
  await verifyServedBundle(bundle);
  const urls = new Map<string, string>();
  for (const asset of bundle.metadata.assets) {
    const bytes = bundle.files[asset.path];
    const copy = new Uint8Array(bytes.byteLength);
    copy.set(bytes);
    urls.set(asset.path, URL.createObjectURL(new Blob([copy.buffer])));
  }
  try {
    const source = new TextDecoder("utf-8", { fatal: true }).decode(bundle.files[bundle.metadata.entrypoint]);
    const documentNode = new DOMParser().parseFromString(source, "text/html");
    if (!documentNode.documentElement || documentNode.querySelector("base,iframe,frame,frameset,object,embed,form,portal")) {
      throw new Error("served bundle contains a forbidden embedding element");
    }
    for (const element of documentNode.querySelectorAll("*")) {
      for (const attribute of [...element.attributes]) {
        const name = attribute.name.toLowerCase();
        if (name.startsWith("on") || ["style", "srcdoc", "formaction", "target", "download", "ping"].includes(name)) {
          throw new Error("served bundle contains a forbidden active attribute");
        }
      }
    }
    if (documentNode.querySelector('meta[http-equiv],script:not([src]),style')) {
      throw new Error("served bundle contains forbidden inline policy or code");
    }
    for (const script of documentNode.querySelectorAll<HTMLScriptElement>("script[src]")) {
      const path = script.getAttribute("src") ?? "";
      const assetUrl = urls.get(path);
      if (!assetUrl || !/\.(?:js|mjs)$/i.test(path)) throw new Error("served bundle references an unknown script");
      script.src = assetUrl;
      script.referrerPolicy = "no-referrer";
    }
    for (const link of documentNode.querySelectorAll<HTMLLinkElement>("link")) {
      const path = link.getAttribute("href") ?? "";
      const assetUrl = urls.get(path);
      if (link.rel !== "stylesheet" || !assetUrl || !/\.css$/i.test(path)) {
        throw new Error("served bundle references an unknown linked asset");
      }
      link.href = assetUrl;
      link.referrerPolicy = "no-referrer";
    }
    for (const media of documentNode.querySelectorAll<HTMLElement>("img[src],audio[src],video[src],source[src]")) {
      const path = media.getAttribute("src") ?? "";
      const assetUrl = urls.get(path);
      if (!assetUrl) throw new Error("served bundle references an unknown media asset");
      media.setAttribute("src", assetUrl);
    }
    for (const anchor of documentNode.querySelectorAll<HTMLAnchorElement>("a[href]")) {
      if (!(anchor.getAttribute("href") ?? "").startsWith("#")) {
        throw new Error("served bundle navigation is forbidden");
      }
    }
    const head = documentNode.head;
    const csp = documentNode.createElement("meta");
    csp.httpEquiv = "Content-Security-Policy";
    csp.content = strictCsp();
    head.prepend(csp);
    const address = documentNode.createElement("meta");
    address.name = "poison-content-address";
    address.content = bundle.metadata.contentSha256;
    head.append(address);
    if (options?.brokerNonce) {
      const nonce = documentNode.createElement("meta");
      nonce.name = "poison-broker-nonce";
      nonce.content = options.brokerNonce;
      head.append(nonce);
    }
    const rewritten = `<!doctype html>${documentNode.documentElement.outerHTML}`;
    const objectUrl = URL.createObjectURL(new Blob([rewritten], { type: "text/html" }));
    const fragment = new URLSearchParams({ content: bundle.metadata.contentSha256 });
    if (options?.brokerNonce) fragment.set("nonce", options.brokerNonce);
    return {
      url: `${objectUrl}#${fragment.toString()}`,
      revoke: () => {
        URL.revokeObjectURL(objectUrl);
        for (const assetUrl of urls.values()) URL.revokeObjectURL(assetUrl);
      },
    };
  } catch (error) {
    for (const url of urls.values()) URL.revokeObjectURL(url);
    throw error;
  }
}

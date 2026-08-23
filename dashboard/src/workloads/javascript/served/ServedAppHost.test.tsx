import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { CommandStatus, MainSchema, type Main } from "../../../generated/flipper_pb";
import { JsBundleFrameKind, JsBundleFrameSchema, JsBundleOperation, JsBundleStatusSchema } from "../../../generated/poison_workload_pb";
import { BundleClient } from "./BundleClient";
import { verifyServedBundle, strictCsp, type ServedBundlePayload } from "./BundleVerifier";
import { MessageBroker } from "./MessageBroker";

const bytes = new TextEncoder().encode("<html><head></head><body>ok</body></html>");
const makeBundle = (): { metadata: { id: string; version: string; apiVersion: number; entrypoint: string; contentSha256: string; size: number; requestedCapabilities: string[]; assets: { path: string; sha256: string; size: number }[] }; files: Record<string, Uint8Array> } => ({ metadata: { id: "org.example.ui", version: "1.0.0", apiVersion: 1, entrypoint: "index.html", contentSha256: "", size: bytes.byteLength, requestedCapabilities: [], assets: [{ path: "index.html", sha256: "", size: bytes.byteLength }] }, files: { "index.html": bytes } });

async function packageDigest(path: string, content: Uint8Array): Promise<string> {
  const pathBytes = new TextEncoder().encode(path);
  const canonical = new Uint8Array(pathBytes.byteLength + 1 + content.byteLength);
  canonical.set(pathBytes);
  canonical.set(content, pathBytes.byteLength + 1);
  const { sha256 } = await import("./BundleVerifier");
  return sha256(canonical);
}

describe("served device interfaces", () => {
  it("requires verified asset and entrypoint digests", async () => {
    const bundle = makeBundle();
    const { sha256 } = await import("./BundleVerifier");
    bundle.metadata.contentSha256 = await packageDigest("index.html", bytes);
    bundle.metadata.assets[0].sha256 = await sha256(bytes);
    await expect(verifyServedBundle(bundle)).resolves.toBeUndefined();
    bundle.files["index.html"] = new TextEncoder().encode("tampered");
    await expect(verifyServedBundle(bundle)).rejects.toThrow();
  });
  it("rejects traversal segments even when the path contains only punctuation", async () => {
    const bundle = makeBundle();
    const { sha256 } = await import("./BundleVerifier");
    bundle.metadata.entrypoint = "../index.html";
    bundle.metadata.assets[0] = { path: "../index.html", sha256: await sha256(bytes), size: bytes.byteLength };
    bundle.metadata.contentSha256 = await packageDigest("../index.html", bytes);
    await expect(verifyServedBundle(bundle)).rejects.toThrow("invalid served bundle metadata");
  });
  it("uses a no-network CSP and rejects forged broker messages", () => {
    expect(strictCsp()).toContain("connect-src 'none'");
    const broker = new MessageBroker("nonce", new Set(["device.status.read"]));
    const source = { postMessage: () => {} } as unknown as WindowProxy;
    broker.bind(source);
    expect(broker.acceptsEnvelope({ source, origin: "null" })).toBe(true);
    expect(broker.acceptsEnvelope({ source, origin: "https://dashboard.invalid" })).toBe(false);
    const request = { type: "poison.request", nonce: "nonce", sequence: 0, capability: "device.status.read", operation: "read", payload: {} } as const;
    expect(broker.accept(request)).toEqual(request);
    expect(() => broker.accept(request)).toThrow();
    expect(() => broker.accept({ ...request, sequence: 1, capability: "evidence.create", operation: "create" })).toThrow();
    expect(() => broker.accept({ ...request, sequence: 1, unexpected: true })).toThrow();
  });
  it("uses canonical path order for multi-asset content digests", async () => {
    const { sha256 } = await import("./BundleVerifier");
    const html = new TextEncoder().encode("<html><head></head><body></body></html>");
    const script = new TextEncoder().encode("globalThis.loaded = true;");
    const canonical = async (): Promise<string> => {
      const chunks = [new TextEncoder().encode("a.js"), new Uint8Array([0]), script,
        new TextEncoder().encode("z.html"), new Uint8Array([0]), html];
      const bytes = new Uint8Array(chunks.reduce((total, chunk) => total + chunk.byteLength, 0));
      let offset = 0;
      for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.byteLength; }
      return sha256(bytes);
    };
    const bundle: ServedBundlePayload = {
      metadata: {
        id: "org.example.order",
        version: "1.0.0",
        apiVersion: 1,
        entrypoint: "z.html",
        contentSha256: await canonical(),
        size: html.byteLength + script.byteLength,
        requestedCapabilities: [],
        assets: [
          { path: "z.html", sha256: await sha256(html), size: html.byteLength },
          { path: "a.js", sha256: await sha256(script), size: script.byteLength },
        ],
      },
      files: { "z.html": html, "a.js": script },
    };
    await expect(verifyServedBundle(bundle)).resolves.toBeUndefined();
  });
  it("loads signed inventory and contiguous asset bytes through device RPC", async () => {
    const content = new Uint8Array(500);
    content.fill(0x41);
    const { sha256 } = await import("./BundleVerifier");
    const assetSha256 = await sha256(content);
    const contentSha256 = await packageDigest("index.html", content);
    const session = {
      async requestStream(request: Main): Promise<readonly Main[]> {
        if (request.content.case !== "poisonJsBundleRequest") throw new Error("unexpected request");
        const input = request.content.value;
        const status = create(MainSchema, {
          commandId: request.commandId,
          commandStatus: CommandStatus.OK,
          content: { case: "poisonJsBundleStatus", value: create(JsBundleStatusSchema, {
            accepted: true,
            message: "verified",
            bundleId: "org.example.ui",
            version: "1.0.0",
            apiVersion: 1,
            entrypoint: "index.html",
            contentSha256,
            size: content.byteLength,
            assetCount: 1,
            capabilityCount: 1,
          }) },
        });
        if (input.operation === JsBundleOperation.DESCRIBE) {
          return [
            create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, hasNext: true, content: { case: "poisonJsBundleFrame", value: create(JsBundleFrameSchema, { kind: JsBundleFrameKind.JS_BUNDLE_FRAME_CAPABILITY, capability: "device.status.read" }) } }),
            create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, hasNext: true, content: { case: "poisonJsBundleFrame", value: create(JsBundleFrameSchema, { kind: JsBundleFrameKind.JS_BUNDLE_FRAME_ASSET, assetPath: "index.html", assetSha256, assetSize: content.byteLength }) } }),
            status,
          ];
        }
        const start = input.offset;
        const end = Math.min(content.byteLength, start + input.length);
        const frames: Main[] = [];
        for (let offset = start; offset < end; offset += 384) {
          const data = content.slice(offset, Math.min(end, offset + 384));
          frames.push(create(MainSchema, {
            commandId: request.commandId,
            commandStatus: CommandStatus.OK,
            hasNext: true,
            content: { case: "poisonJsBundleFrame", value: create(JsBundleFrameSchema, {
              kind: JsBundleFrameKind.JS_BUNDLE_FRAME_DATA,
              assetPath: "index.html",
              assetSha256,
              assetSize: content.byteLength,
              offset,
              data,
              eof: offset + data.byteLength === content.byteLength,
            }) },
          }));
        }
        return [...frames, status];
      },
    };
    const loaded = await new BundleClient(session).loadBundle({
      id: "org.example.ui",
      version: "1.0.0",
      contentSha256,
    });
    expect(loaded.metadata.requestedCapabilities).toEqual(["device.status.read"]);
    expect(loaded.files["index.html"]).toEqual(content);
  });
});

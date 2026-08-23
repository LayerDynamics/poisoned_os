import { describe, expect, it } from "vitest";
import {
  derEcdsaToP1363,
  downloadRelease,
  loadInstallerRuntimeConfig,
  loadReleaseFeed,
  signedManifestPayload,
  validateFeed,
  validateInstallerRuntimeConfig,
  verifyReleaseSignature,
  type SignedReleaseManifest,
} from "./release-feed";
import { sha256 } from "./archive";

function base64(bytes: Uint8Array): string {
  return btoa(String.fromCharCode(...bytes));
}

function rawToDer(raw: Uint8Array): Uint8Array {
  const integers = [raw.subarray(0, 32), raw.subarray(32)].map((part) => {
    let first = 0;
    while (first < part.byteLength - 1 && part[first] === 0) first += 1;
    const value = part.subarray(first);
    const prefix = (value[0]! & 0x80) === 0 ? 0 : 1;
    const encoded = new Uint8Array(2 + prefix + value.byteLength);
    encoded[0] = 0x02;
    encoded[1] = prefix + value.byteLength;
    if (prefix) encoded[2] = 0;
    encoded.set(value, 2 + prefix);
    return encoded;
  });
  const der = new Uint8Array(2 + integers[0]!.byteLength + integers[1]!.byteLength);
  der[0] = 0x30;
  der[1] = der.byteLength - 2;
  der.set(integers[0]!, 2);
  der.set(integers[1]!, 2 + integers[0]!.byteLength);
  return der;
}

async function signedFixture(packageBytes = Uint8Array.from([1, 2, 3, 4])): Promise<{
  manifest: SignedReleaseManifest;
  publicPem: string;
}> {
  const pair = await crypto.subtle.generateKey({ name: "ECDSA", namedCurve: "P-256" }, true, ["sign", "verify"]);
  const spki = new Uint8Array(await crypto.subtle.exportKey("spki", pair.publicKey));
  const publicPem = `-----BEGIN PUBLIC KEY-----\n${base64(spki)}\n-----END PUBLIC KEY-----`;
  const manifest = {
    schema: "poison.release-manifest/v1",
    version: "1.2.3",
    channel: "stable",
    target: "f7",
    rollbackVersion: "1.2.2",
    components: [{
      id: "firmware.update.tgz",
      path: "dist/flipper-z-f7-update-poisonedos.tgz",
      sha256: await sha256(packageBytes),
      bytes: packageBytes.byteLength,
    }],
    revocations: [],
    signature: { algorithm: "ECDSA-P256-SHA256", keyId: "firmware-test-1", value: "MAQCAQE=" },
  } satisfies SignedReleaseManifest;
  const payload = signedManifestPayload(manifest);
  const raw = new Uint8Array(await crypto.subtle.sign(
    { name: "ECDSA", hash: "SHA-256" },
    pair.privateKey,
    payload.buffer.slice(payload.byteOffset, payload.byteOffset + payload.byteLength) as ArrayBuffer,
  ));
  return {
    manifest: { ...manifest, signature: { ...manifest.signature, value: base64(rawToDer(raw)) } },
    publicPem,
  };
}

describe("signed web installer release feed", () => {
  it("loads the Railway-generated runtime feed and trust configuration", async () => {
    const config = {
      schema: "poison.web-installer-config/v1",
      releaseFeedUrl: "../releases.json",
      trustedReleaseKeys: { "firmware-test-1": "-----BEGIN PUBLIC KEY-----\nAAAA\n-----END PUBLIC KEY-----" },
    };
    expect(() => validateInstallerRuntimeConfig(config)).toThrowError(/unsafe/);

    const { publicPem } = await signedFixture();
    const valid = { ...config, releaseFeedUrl: "./releases.json", trustedReleaseKeys: { "firmware-test-1": publicPem } };
    expect(validateInstallerRuntimeConfig(valid)).toEqual(valid);
    const fetcher = async () => new Response(JSON.stringify(valid), { status: 200 });
    await expect(loadInstallerRuntimeConfig(fetcher as typeof fetch)).resolves.toEqual(valid);
  });

  it("keeps local package mode when Railway has no generated config", async () => {
    const fetcher = async () => new Response("not found", { status: 404 });
    await expect(loadInstallerRuntimeConfig(fetcher as typeof fetch)).resolves.toBeNull();
  });

  it("validates the feed, binds the tgz component, and verifies its P-256 signature", async () => {
    const { manifest, publicPem } = await signedFixture();
    const feed = validateFeed({
      schema: "poison.web-installer-feed/v1",
      releases: [{ manifest, packageComponentId: "firmware.update.tgz", packageUrl: "releases/update.tgz" }],
    }, "https://install.poisoned.example/releases.json");
    expect(feed.releases[0]!.resolvedPackageUrl).toBe("https://install.poisoned.example/releases/update.tgz");
    await expect(verifyReleaseSignature(feed.releases[0]!.manifest, { "firmware-test-1": publicPem })).resolves.toBeUndefined();
  });

  it("rejects signed-manifest tampering", async () => {
    const { manifest, publicPem } = await signedFixture();
    const tampered = { ...manifest, version: "9.9.9" };
    await expect(verifyReleaseSignature(tampered, { "firmware-test-1": publicPem }))
      .rejects.toThrowError(/invalid signature/);
  });

  it("downloads exactly the signed byte count and digest", async () => {
    const packageBytes = Uint8Array.from([8, 7, 6, 5]);
    const { manifest } = await signedFixture(packageBytes);
    const release = validateFeed({
      schema: "poison.web-installer-feed/v1",
      releases: [{ manifest, packageComponentId: "firmware.update.tgz", packageUrl: "https://cdn.example/update.tgz" }],
    }, "https://install.example/releases.json").releases[0]!;
    const progress: number[] = [];
    const fetcher = async () => new Response(packageBytes, {
      status: 200,
      headers: { "content-length": String(packageBytes.byteLength) },
    });
    await expect(downloadRelease(release, (received) => progress.push(received), fetcher as typeof fetch))
      .resolves.toEqual(packageBytes);
    expect(progress.at(-1)).toBe(packageBytes.byteLength);
  });

  it("rejects a package whose bytes do not match the signed digest", async () => {
    const { manifest } = await signedFixture(Uint8Array.from([1, 2, 3, 4]));
    const release = validateFeed({
      schema: "poison.web-installer-feed/v1",
      releases: [{ manifest, packageComponentId: "firmware.update.tgz", packageUrl: "https://cdn.example/update.tgz" }],
    }, "https://install.example/releases.json").releases[0]!;
    const fetcher = async () => new Response(Uint8Array.from([4, 3, 2, 1]));
    await expect(downloadRelease(release, undefined, fetcher as typeof fetch)).rejects.toThrowError(/SHA-256/);
  });

  it("allows unrelated large release components while bounding the selected browser package", async () => {
    const { manifest } = await signedFixture();
    const withLargeSbom = {
      ...manifest,
      components: [...manifest.components, {
        id: "offline.builder",
        path: "platform/offline-builder.tgz",
        sha256: "b".repeat(64),
        bytes: 64 * 1024 * 1024,
      }],
    };
    expect(() => validateFeed({
      schema: "poison.web-installer-feed/v1",
      releases: [{ manifest: withLargeSbom, packageComponentId: "firmware.update.tgz", packageUrl: "releases/update.tgz" }],
    }, "https://install.example/releases.json")).not.toThrow();
    expect(() => validateFeed({
      schema: "poison.web-installer-feed/v1",
      releases: [{ manifest: withLargeSbom, packageComponentId: "offline.builder", packageUrl: "releases/update.tgz" }],
    }, "https://install.example/releases.json")).toThrowError(/browser size limit/);
  });

  it("rejects malformed ECDSA DER", () => {
    expect(() => derEcdsaToP1363(Uint8Array.from([0x30, 0x01, 0x00])))
      .toThrowError(/DER integer/);
  });

  it("stops reading a release feed after the 512 KiB limit", async () => {
    const oversized = new Uint8Array(512 * 1024 + 1);
    const fetcher = async () => new Response(oversized, { status: 200 });
    await expect(loadReleaseFeed("https://install.example/releases.json", {}, fetcher as typeof fetch))
      .rejects.toThrowError(/Release feed is too large/);
  });
});

import { create } from "@bufbuild/protobuf";
import { CommandStatus, MainSchema, type Main } from "../../../generated/flipper_pb";
import {
  JsBundleFrameKind,
  JsBundleOperation,
  JsBundleRequestSchema,
  type JsBundleStatus,
} from "../../../generated/poison_workload_pb";
import {
  MAX_ASSETS,
  MAX_BUNDLE_BYTES,
  verifyServedBundle,
  type ServedBundleAsset,
  type ServedBundlePayload,
} from "./BundleVerifier";

const DIGEST = /^[0-9a-f]{64}$/;
const MAX_CAPABILITIES = 16;
const READ_BYTES = 12 * 1024;
const MAX_READ_FRAMES = 34;

export interface BundleRpcSession {
  requestStream(request: Main, maxResponses?: number, signal?: AbortSignal): Promise<readonly Main[]>;
}

export interface BundleIdentity {
  readonly id: string;
  readonly version: string;
  readonly contentSha256: string;
}

export class BundleClientError extends Error {
  public constructor(
    public readonly code: "invalid" | "protocol" | "rejected" | "digest",
    message: string,
  ) {
    super(message);
    this.name = "BundleClientError";
  }
}

function validateIdentity(identity: BundleIdentity): void {
  if (!/^[a-z0-9][a-z0-9._-]{0,63}$/.test(identity.id) ||
      identity.version.length < 1 || identity.version.length > 32 ||
      !DIGEST.test(identity.contentSha256)) {
    throw new BundleClientError("invalid", "served bundle identity is invalid");
  }
}

function validateStatus(status: JsBundleStatus, identity: BundleIdentity): void {
  if (status.bundleId !== identity.id || status.version !== identity.version ||
      status.contentSha256 !== identity.contentSha256 || status.apiVersion < 1 ||
      status.size < 1 || status.size > MAX_BUNDLE_BYTES ||
      status.capabilityCount > MAX_CAPABILITIES || status.assetCount < 1 ||
      status.assetCount > MAX_ASSETS) {
    throw new BundleClientError("protocol", "device returned malformed bundle status");
  }
}

function finalStatus(
  responses: readonly Main[],
  request: Main,
  identity: BundleIdentity,
): JsBundleStatus {
  const final = responses.at(-1);
  if (!final || final.commandId !== request.commandId || final.commandStatus !== CommandStatus.OK ||
      final.hasNext || final.content.case !== "poisonJsBundleStatus") {
    throw new BundleClientError("protocol", "device omitted final served bundle status");
  }
  if (!final.content.value.accepted) {
    throw new BundleClientError(
      "rejected",
      final.content.value.message || "device rejected served bundle request",
    );
  }
  validateStatus(final.content.value, identity);
  return final.content.value;
}

export class BundleClient {
  private nextCommandId = 28_000;

  public constructor(private readonly session: BundleRpcSession) {}

  public async loadBundle(identity: BundleIdentity, signal?: AbortSignal): Promise<ServedBundlePayload> {
    validateIdentity(identity);
    const described = await this.describe(identity, signal);
    const files: Record<string, Uint8Array> = {};
    for (const asset of described.assets) {
      files[asset.path] = asset.size === 0
        ? new Uint8Array()
        : await this.readAsset(identity, asset, signal);
    }
    const payload: ServedBundlePayload = {
      metadata: {
        id: identity.id,
        version: identity.version,
        apiVersion: described.status.apiVersion,
        entrypoint: described.status.entrypoint,
        contentSha256: identity.contentSha256,
        size: described.status.size,
        requestedCapabilities: described.capabilities,
        assets: described.assets,
      },
      files,
    };
    try {
      await verifyServedBundle(payload);
    } catch (error) {
      throw new BundleClientError(
        "digest",
        error instanceof Error ? error.message : "served bundle verification failed",
      );
    }
    return payload;
  }

  private async describe(identity: BundleIdentity, signal?: AbortSignal): Promise<{
    readonly status: JsBundleStatus;
    readonly capabilities: readonly string[];
    readonly assets: readonly ServedBundleAsset[];
  }> {
    const request = this.request(identity, JsBundleOperation.DESCRIBE);
    const responses = await this.session.requestStream(
      request,
      1 + MAX_CAPABILITIES + MAX_ASSETS,
      signal,
    );
    const status = finalStatus(responses, request, identity);
    const capabilities: string[] = [];
    const assets: ServedBundleAsset[] = [];
    const capabilityNames = new Set<string>();
    const assetPaths = new Set<string>();
    for (const response of responses.slice(0, -1)) {
      if (response.commandId !== request.commandId || response.commandStatus !== CommandStatus.OK ||
          !response.hasNext || response.content.case !== "poisonJsBundleFrame") {
        throw new BundleClientError("protocol", "device returned malformed bundle inventory");
      }
      const frame = response.content.value;
      if (frame.kind === JsBundleFrameKind.JS_BUNDLE_FRAME_CAPABILITY && frame.capability.length > 0 &&
          frame.capability.length <= 64 && !capabilityNames.has(frame.capability) &&
          frame.assetPath === "" && frame.assetSha256 === "" && frame.data.byteLength === 0) {
        capabilityNames.add(frame.capability);
        capabilities.push(frame.capability);
      } else if (frame.kind === JsBundleFrameKind.JS_BUNDLE_FRAME_ASSET && frame.capability === "" &&
                 frame.assetPath.length > 0 && frame.assetPath.length <= 256 &&
                 !assetPaths.has(frame.assetPath) && DIGEST.test(frame.assetSha256) &&
                 Number.isSafeInteger(frame.assetSize) && frame.assetSize >= 0 &&
                 frame.assetSize <= MAX_BUNDLE_BYTES && frame.offset === 0 &&
                 frame.data.byteLength === 0 && !frame.eof) {
        assetPaths.add(frame.assetPath);
        assets.push({ path: frame.assetPath, sha256: frame.assetSha256, size: frame.assetSize });
      } else {
        throw new BundleClientError("protocol", "device returned invalid bundle inventory frame");
      }
    }
    if (capabilities.length !== status.capabilityCount || assets.length !== status.assetCount) {
      throw new BundleClientError("protocol", "bundle inventory count does not match status");
    }
    return { status, capabilities, assets };
  }

  private async readAsset(
    identity: BundleIdentity,
    asset: ServedBundleAsset,
    signal?: AbortSignal,
  ): Promise<Uint8Array> {
    const output = new Uint8Array(asset.size);
    let offset = 0;
    while (offset < asset.size) {
      const length = Math.min(READ_BYTES, asset.size - offset);
      const request = this.request(identity, JsBundleOperation.READ_ASSET, asset.path, offset, length);
      const responses = await this.session.requestStream(request, MAX_READ_FRAMES, signal);
      finalStatus(responses, request, identity);
      const dataFrames = responses.slice(0, -1);
      if (dataFrames.length < 1 || dataFrames.length > 32) {
        throw new BundleClientError("protocol", "device returned an invalid asset frame count");
      }
      let received = 0;
      for (const [index, response] of dataFrames.entries()) {
        if (response.commandId !== request.commandId || response.commandStatus !== CommandStatus.OK ||
            !response.hasNext || response.content.case !== "poisonJsBundleFrame") {
          throw new BundleClientError("protocol", "device returned malformed asset data");
        }
        const frame = response.content.value;
        if (frame.kind !== JsBundleFrameKind.JS_BUNDLE_FRAME_DATA || frame.capability !== "" ||
            frame.assetPath !== asset.path || frame.assetSha256 !== asset.sha256 ||
            frame.assetSize !== asset.size || frame.offset !== offset + received ||
            frame.data.byteLength < 1 || frame.data.byteLength > 384 ||
            frame.eof !== (offset + received + frame.data.byteLength === asset.size) ||
            (frame.eof && index !== dataFrames.length - 1)) {
          throw new BundleClientError("protocol", "device returned non-contiguous asset data");
        }
        output.set(frame.data, frame.offset);
        received += frame.data.byteLength;
      }
      if (received !== length) {
        throw new BundleClientError("protocol", "device returned a short asset read");
      }
      offset += received;
    }
    return output;
  }

  private request(
    identity: BundleIdentity,
    operation: JsBundleOperation,
    assetPath = "",
    offset = 0,
    length = 0,
  ): Main {
    return create(MainSchema, {
      commandId: this.nextCommandId++,
      content: {
        case: "poisonJsBundleRequest",
        value: create(JsBundleRequestSchema, {
          operation,
          bundleId: identity.id,
          version: identity.version,
          contentSha256: identity.contentSha256,
          assetPath,
          offset,
          length,
        }),
      },
    });
  }
}

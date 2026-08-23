export type WorkloadState =
  | "queued"
  | "starting"
  | "running"
  | "cancelling"
  | "completed"
  | "failed"
  | "cancelled"
  | "timed-out"
  | "crashed"
  | "disconnected";

export interface JavaScriptLimits { heapBytes: number; wallTimeMs: number; logBytes: number; artifactBytes: number; }
export interface JavaScriptManifest {
  format: 1;
  id: string;
  name: string;
  version: string;
  language: "javascript";
  entrypoint: string;
  runtime: "poison-mjs-1";
  runtimeApi: number;
  firmwareApi: string;
  capabilities: readonly string[];
  limits: JavaScriptLimits;
  dependencies: string;
  servedUi: null | { bundleId: string; version: string; contentSha256: string };
}

const ID = /^[a-z0-9]+(?:[._-][a-z0-9]+)*$/;
const ENTRYPOINT = /^(?:[a-zA-Z0-9._-]+\/)*[a-zA-Z0-9._-]+\.m?js$/;
const DIGIT_VERSION = /^\d+(?:\.\d+){0,2}$/;
const CAPABILITY = /^[a-z][a-z0-9._-]{0,63}$/;
const DIGEST = /^[0-9a-f]{64}$/;

function boundedPositive(value: unknown, maximum: number): value is number {
  return typeof value === "number" && Number.isSafeInteger(value) && value > 0 && value <= maximum;
}

export function validateJavaScriptManifest(value: unknown): JavaScriptManifest {
  if (!value || typeof value !== "object") throw new Error("manifest must be an object");
  const candidate = value as Partial<JavaScriptManifest>;
  if (candidate.format !== 1 || candidate.language !== "javascript" || candidate.runtime !== "poison-mjs-1" ||
      typeof candidate.id !== "string" || !ID.test(candidate.id) || candidate.id.length > 64 ||
      typeof candidate.name !== "string" || candidate.name.length < 1 || candidate.name.length > 96 ||
      typeof candidate.version !== "string" || !DIGIT_VERSION.test(candidate.version) ||
      typeof candidate.entrypoint !== "string" || !ENTRYPOINT.test(candidate.entrypoint) ||
      typeof candidate.runtimeApi !== "number" || !Number.isSafeInteger(candidate.runtimeApi) || candidate.runtimeApi < 1 ||
      typeof candidate.firmwareApi !== "string" || candidate.firmwareApi.length > 64 ||
      !Array.isArray(candidate.capabilities) || candidate.capabilities.some((item) => typeof item !== "string" || !CAPABILITY.test(item)) ||
      !candidate.limits || !boundedPositive(candidate.limits.heapBytes, 1024 * 1024) ||
      !boundedPositive(candidate.limits.wallTimeMs, 10 * 60 * 1000) || !boundedPositive(candidate.limits.logBytes, 1024 * 1024) ||
      !boundedPositive(candidate.limits.artifactBytes, 8 * 1024 * 1024) || typeof candidate.dependencies !== "string" ||
      candidate.dependencies !== "poison-js.lock" ||
      (candidate.servedUi !== null && candidate.servedUi !== undefined &&
       (typeof candidate.servedUi !== "object" ||
        typeof candidate.servedUi.bundleId !== "string" || !ID.test(candidate.servedUi.bundleId) ||
        candidate.servedUi.bundleId.length > 64 || typeof candidate.servedUi.version !== "string" ||
        !DIGIT_VERSION.test(candidate.servedUi.version) ||
        typeof candidate.servedUi.contentSha256 !== "string" ||
        !DIGEST.test(candidate.servedUi.contentSha256)))) {
    throw new Error("invalid JavaScript manifest");
  }
  return {
    ...candidate,
    servedUi: candidate.servedUi ? { ...candidate.servedUi } : null,
    capabilities: [...candidate.capabilities],
  } as JavaScriptManifest;
}

export function clampJavaScriptLimits(limits: JavaScriptLimits, policy: JavaScriptLimits): JavaScriptLimits {
  return {
    heapBytes: Math.min(limits.heapBytes, policy.heapBytes),
    wallTimeMs: Math.min(limits.wallTimeMs, policy.wallTimeMs),
    logBytes: Math.min(limits.logBytes, policy.logBytes),
    artifactBytes: Math.min(limits.artifactBytes, policy.artifactBytes),
  };
}

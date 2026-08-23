import { create } from "@bufbuild/protobuf";
import { useEffect, useState, type ReactElement } from "react";
import { LockStatusRequestSchema } from "../generated/application_pb";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import { InfoRequestSchema } from "../generated/storage_pb";
import {
  DeviceInfoRequestSchema,
  PowerInfoRequestSchema,
  ProtobufVersionRequestSchema,
} from "../generated/system_pb";
import type { TransportHealth, TransportKind } from "../transports/Transport";

export interface DeviceVersion { major: number; minor: number; }

export interface DeviceStatus {
  firmwareVersion: string;
  apiVersion: DeviceVersion;
  protocolVersion: DeviceVersion;
  batteryPercent: number;
  charging: boolean;
  internalBytes: number;
  internalFreeBytes: number;
  sdPresent: boolean;
  sdBytes: number;
  sdFreeBytes: number;
  activeApplication: string | null;
  locked: boolean;
  transport: TransportKind;
  transportHealthy: boolean;
  observedAtMs: number;
}

export function validateDeviceStatus(status: DeviceStatus): void {
  const validVersion = (version: DeviceVersion) => Number.isInteger(version.major) && version.major >= 0 && version.major <= 0xffff &&
    Number.isInteger(version.minor) && version.minor >= 0 && version.minor <= 0xffff;
  if (!status.firmwareVersion || new TextEncoder().encode(status.firmwareVersion).byteLength > 128 ||
      status.firmwareVersion.includes("\0") || !validVersion(status.apiVersion) || !validVersion(status.protocolVersion)) {
    throw new Error("invalid firmware status");
  }
  if (!Number.isInteger(status.batteryPercent) || status.batteryPercent < 0 || status.batteryPercent > 100) throw new Error("invalid battery status");
  const storageValues = [status.internalBytes, status.internalFreeBytes, status.sdBytes, status.sdFreeBytes];
  if (storageValues.some((value) => !Number.isSafeInteger(value) || value < 0) ||
      status.internalFreeBytes > status.internalBytes || status.sdFreeBytes > status.sdBytes ||
      (!status.sdPresent && (status.sdBytes !== 0 || status.sdFreeBytes !== 0))) {
    throw new Error("invalid storage status");
  }
  if (status.activeApplication !== null && (!status.activeApplication || status.activeApplication.includes("\0") ||
      new TextEncoder().encode(status.activeApplication).byteLength > 128)) throw new Error("invalid application status");
  if (!Number.isSafeInteger(status.observedAtMs) || status.observedAtMs < 0) throw new Error("invalid observation timestamp");
}

export function DeviceStatusCard({ status }: { status: DeviceStatus }): ReactElement {
  validateDeviceStatus(status);
  const storage = status.sdPresent ?
    `${status.sdFreeBytes.toLocaleString()} of ${status.sdBytes.toLocaleString()} SD bytes free` : "SD not mounted";
  return <section className="update-card device-status" aria-label="Device status">
    <div className="update-heading">
      <div><p className="eyebrow">LIVE ENCRYPTED DEVICE STATUS</p><h2>{status.firmwareVersion}</h2></div>
      <output className="update-state">{status.transportHealthy ? `${status.transport} healthy` : `${status.transport} error`}</output>
    </div>
    <p>Firmware API {status.apiVersion.major}.{status.apiVersion.minor} · RPC {status.protocolVersion.major}.{status.protocolVersion.minor}</p>
    <p>{status.batteryPercent}% battery · {status.charging ? "charging" : "discharging"} · {status.activeApplication ?? "Home"}{status.locked ? " · loader locked" : ""}</p>
    <p>{status.internalFreeBytes.toLocaleString()} of {status.internalBytes.toLocaleString()} internal bytes free · {storage}</p>
  </section>;
}

export interface DeviceStatusSession {
  readonly transportKind: TransportKind | null;
  readonly transportHealth: TransportHealth | null;
  request(request: Main, signal?: AbortSignal): Promise<Main>;
  requestStream(request: Main, maxResponses?: number, signal?: AbortSignal): Promise<readonly Main[]>;
}

function statusEntries(responses: readonly Main[], expected: "systemDeviceInfoResponse" | "systemPowerInfoResponse"): ReadonlyMap<string, string> {
  const entries = new Map<string, string>();
  for (const response of responses) {
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== expected) {
      throw new Error(`device returned an invalid ${expected} stream`);
    }
    const { key, value } = response.content.value;
    if (!key || key.includes("\0") || value.includes("\0") || new TextEncoder().encode(key).byteLength > 128 ||
        new TextEncoder().encode(value).byteLength > 512 || entries.has(key)) {
      throw new Error(`device returned invalid or duplicate status property ${key || "<empty>"}`);
    }
    entries.set(key, value);
  }
  return entries;
}

function required(entries: ReadonlyMap<string, string>, key: string): string {
  const value = entries.get(key);
  if (value === undefined) throw new Error(`device status is missing ${key}`);
  return value;
}

function decimal(value: string, field: string, maximum = Number.MAX_SAFE_INTEGER): number {
  if (!/^(0|[1-9]\d*)$/.test(value)) throw new Error(`device returned invalid ${field}`);
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed > maximum) throw new Error(`device returned invalid ${field}`);
  return parsed;
}

function storageNumber(value: bigint, field: string): number {
  if (value < 0n || value > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error(`device returned invalid ${field}`);
  return Number(value);
}

export class DeviceStatusClient {
  private nextCommandId = 30_000;

  public constructor(private readonly session: DeviceStatusSession, private readonly now: () => number = Date.now) {}

  public async read(signal?: AbortSignal): Promise<DeviceStatus> {
    const deviceResponses = await this.session.requestStream(create(MainSchema, {
      commandId: this.reserveCommandId(),
      content: { case: "systemDeviceInfoRequest", value: create(DeviceInfoRequestSchema) },
    }), 64, signal);
    const powerResponses = await this.session.requestStream(create(MainSchema, {
      commandId: this.reserveCommandId(),
      content: { case: "systemPowerInfoRequest", value: create(PowerInfoRequestSchema) },
    }), 32, signal);
    const protocolResponse = await this.session.request(create(MainSchema, {
      commandId: this.reserveCommandId(),
      content: { case: "systemProtobufVersionRequest", value: create(ProtobufVersionRequestSchema) },
    }), signal);
    const internalResponse = await this.storageInfo("/int", signal);
    const externalResponse = await this.storageInfo("/ext", signal, true);
    if (!internalResponse) throw new Error("internal storage status is unavailable");
    const appResponse = await this.session.request(create(MainSchema, {
      commandId: this.reserveCommandId(),
      content: { case: "appLockStatusRequest", value: create(LockStatusRequestSchema) },
    }), signal);

    const device = statusEntries(deviceResponses, "systemDeviceInfoResponse");
    const power = statusEntries(powerResponses, "systemPowerInfoResponse");
    if (protocolResponse.commandStatus !== CommandStatus.OK || protocolResponse.content.case !== "systemProtobufVersionResponse") {
      throw new Error("device returned invalid RPC protocol status");
    }
    if (appResponse.commandStatus !== CommandStatus.OK || appResponse.content.case !== "appLockStatusResponse") {
      throw new Error("device returned invalid application status");
    }
    const transport = this.session.transportKind;
    const health = this.session.transportHealth;
    if (!transport || !health) throw new Error("device transport status is unavailable");

    const status: DeviceStatus = {
      firmwareVersion: required(device, "firmware_version"),
      apiVersion: {
        major: decimal(required(device, "firmware_api_major"), "firmware API major", 0xffff),
        minor: decimal(required(device, "firmware_api_minor"), "firmware API minor", 0xffff),
      },
      protocolVersion: {
        major: protocolResponse.content.value.major,
        minor: protocolResponse.content.value.minor,
      },
      batteryPercent: decimal(required(power, "charge_level"), "battery level", 100),
      charging: ["charging", "charged"].includes(required(power, "charge_state")),
      internalBytes: internalResponse.total,
      internalFreeBytes: internalResponse.free,
      sdPresent: externalResponse !== null,
      sdBytes: externalResponse?.total ?? 0,
      sdFreeBytes: externalResponse?.free ?? 0,
      activeApplication: appResponse.content.value.activeApplication || null,
      locked: appResponse.content.value.locked,
      transport,
      transportHealthy: health.connected && health.writable && !health.lastError,
      observedAtMs: this.now(),
    };
    validateDeviceStatus(status);
    return status;
  }

  private async storageInfo(path: "/int" | "/ext", signal?: AbortSignal, optional = false): Promise<{ total: number; free: number } | null> {
    const response = await this.session.request(create(MainSchema, {
      commandId: this.reserveCommandId(),
      content: { case: "storageInfoRequest", value: create(InfoRequestSchema, { path }) },
    }), signal);
    if (optional && response.commandStatus === CommandStatus.ERROR_STORAGE_NOT_READY) return null;
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "storageInfoResponse") {
      throw new Error(`device rejected ${path} storage status`);
    }
    const total = storageNumber(response.content.value.totalSpace, `${path} total space`);
    const free = storageNumber(response.content.value.freeSpace, `${path} free space`);
    if (free > total) throw new Error(`device returned impossible ${path} storage status`);
    return { total, free };
  }

  private reserveCommandId(): number {
    const commandId = this.nextCommandId;
    this.nextCommandId = this.nextCommandId === 0xffffffff ? 30_000 : this.nextCommandId + 1;
    return commandId;
  }
}

export function DeviceStatusPanel({ session }: { session: DeviceStatusSession }): ReactElement {
  const [status, setStatus] = useState<DeviceStatus | null>(null);
  const [error, setError] = useState<string | null>(null);
  useEffect(() => {
    let active = true;
    let timer: ReturnType<typeof setTimeout> | null = null;
    const abort = new AbortController();
    const client = new DeviceStatusClient(session);
    const refresh = async () => {
      try {
        const next = await client.read(abort.signal);
        if (active) { setStatus(next); setError(null); }
      } catch (reason) {
        if (active) setError(reason instanceof Error ? reason.message : String(reason));
      } finally {
        if (active) timer = setTimeout(() => void refresh(), 5_000);
      }
    };
    void refresh();
    return () => {
      active = false;
      abort.abort();
      if (timer) clearTimeout(timer);
    };
  }, [session]);
  if (status) return <><DeviceStatusCard status={status} />{error && <p className="error" role="alert">Status refresh failed: {error}</p>}</>;
  return <section className="update-card device-status" aria-label="Device status"><p role={error ? "alert" : "status"}>{error ?? "Reading encrypted device status…"}</p></section>;
}

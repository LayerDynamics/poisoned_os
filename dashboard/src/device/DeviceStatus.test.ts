import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { LockStatusResponseSchema } from "../generated/application_pb";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import { InfoResponseSchema } from "../generated/storage_pb";
import { DeviceInfoResponseSchema, PowerInfoResponseSchema, ProtobufVersionResponseSchema } from "../generated/system_pb";
import { DeviceStatusClient, validateDeviceStatus, type DeviceStatus, type DeviceStatusSession } from "./DeviceStatus";

const status: DeviceStatus = {
  firmwareVersion: "poison-v1",
  apiVersion: { major: 88, minor: 58 },
  protocolVersion: { major: 0, minor: 24 },
  batteryPercent: 90,
  charging: false,
  internalBytes: 100,
  internalFreeBytes: 50,
  sdPresent: true,
  sdBytes: 10,
  sdFreeBytes: 5,
  activeApplication: null,
  locked: false,
  transport: "serial",
  transportHealthy: true,
  observedAtMs: 1,
};

function streamed(commandId: number, kind: "systemDeviceInfoResponse" | "systemPowerInfoResponse", entries: readonly [string, string][]): Main[] {
  return entries.map(([key, value], index) => create(MainSchema, {
    commandId,
    commandStatus: CommandStatus.OK,
    hasNext: index !== entries.length - 1,
    content: kind === "systemDeviceInfoResponse" ? {
      case: kind,
      value: create(DeviceInfoResponseSchema, { key, value }),
    } : {
      case: kind,
      value: create(PowerInfoResponseSchema, { key, value }),
    },
  }));
}

describe("DeviceStatus", () => {
  it("rejects impossible battery and storage values", () => {
    expect(() => validateDeviceStatus(status)).not.toThrow();
    expect(() => validateDeviceStatus({ ...status, batteryPercent: 101 })).toThrow();
    expect(() => validateDeviceStatus({ ...status, internalFreeBytes: 101 })).toThrow();
    expect(() => validateDeviceStatus({ ...status, sdPresent: false })).toThrow();
  });

  it("reads complete status from registered encrypted firmware handlers", async () => {
    const requests: string[] = [];
    const session: DeviceStatusSession = {
      transportKind: "serial",
      transportHealth: { connected: true, writable: true, queuedBytes: 0 },
      async requestStream(request) {
        requests.push(request.content.case ?? "missing");
        if (request.content.case === "systemDeviceInfoRequest") return streamed(request.commandId, "systemDeviceInfoResponse", [
          ["firmware_version", "poison-v1"], ["firmware_api_major", "88"], ["firmware_api_minor", "58"],
        ]);
        if (request.content.case === "systemPowerInfoRequest") return streamed(request.commandId, "systemPowerInfoResponse", [
          ["charge_level", "73"], ["charge_state", "charging"],
        ]);
        throw new Error("unexpected stream request");
      },
      async request(request) {
        requests.push(request.content.case ?? "missing");
        if (request.content.case === "systemProtobufVersionRequest") return create(MainSchema, {
          commandId: request.commandId, commandStatus: CommandStatus.OK,
          content: { case: "systemProtobufVersionResponse", value: create(ProtobufVersionResponseSchema, { major: 0, minor: 24 }) },
        });
        if (request.content.case === "storageInfoRequest") {
          const external = request.content.value.path === "/ext";
          return create(MainSchema, {
            commandId: request.commandId, commandStatus: CommandStatus.OK,
            content: { case: "storageInfoResponse", value: create(InfoResponseSchema, {
              totalSpace: BigInt(external ? 2_000 : 1_000), freeSpace: BigInt(external ? 1_500 : 400),
            }) },
          });
        }
        if (request.content.case === "appLockStatusRequest") return create(MainSchema, {
          commandId: request.commandId, commandStatus: CommandStatus.OK,
          content: { case: "appLockStatusResponse", value: create(LockStatusResponseSchema, { locked: true, activeApplication: "NFC" }) },
        });
        throw new Error("unexpected request");
      },
    };

    await expect(new DeviceStatusClient(session, () => 7).read()).resolves.toEqual({
      firmwareVersion: "poison-v1",
      apiVersion: { major: 88, minor: 58 },
      protocolVersion: { major: 0, minor: 24 },
      batteryPercent: 73,
      charging: true,
      internalBytes: 1_000,
      internalFreeBytes: 400,
      sdPresent: true,
      sdBytes: 2_000,
      sdFreeBytes: 1_500,
      activeApplication: "NFC",
      locked: true,
      transport: "serial",
      transportHealthy: true,
      observedAtMs: 7,
    });
    expect(requests).toEqual([
      "systemDeviceInfoRequest", "systemPowerInfoRequest", "systemProtobufVersionRequest",
      "storageInfoRequest", "storageInfoRequest", "appLockStatusRequest",
    ]);
  });
});

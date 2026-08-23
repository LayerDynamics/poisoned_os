import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import {
  CommandStatus,
  MainSchema,
  type Main,
} from "../../dashboard/src/generated/flipper_pb";
import { FileSchema, InfoResponseSchema, ReadResponseSchema } from "../../dashboard/src/generated/storage_pb";
import {
  DeviceInfoResponseSchema,
  PingResponseSchema,
  ProtobufVersionResponseSchema,
  UpdateResponseSchema,
  UpdateResponse_UpdateResultCode,
} from "../../dashboard/src/generated/system_pb";
import {
  DelimitedMessageDecoder,
  FlipperRpcClient,
  RpcError,
  encodeDelimited,
} from "./flipper-rpc";
import { sha256 } from "./archive";
import type { ByteTransport } from "./web-serial";

class RpcFixtureTransport implements ByteTransport {
  public readonly requests: Main[] = [];
  public readonly stored = new Map<string, number[]>();
  private readonly outgoing = new DelimitedMessageDecoder();
  private readonly incoming: Uint8Array[] = [];

  public async write(data: Uint8Array): Promise<void> {
    for (const request of this.outgoing.push(data)) {
      this.requests.push(request);
      this.respond(request);
    }
  }

  public async read(): Promise<Uint8Array | null> {
    return this.incoming.shift() ?? null;
  }

  public async close(): Promise<void> {}

  private queue(message: Main): void {
    const frame = encodeDelimited(message);
    const split = Math.min(2, frame.byteLength);
    this.incoming.push(frame.slice(0, split), frame.slice(split));
  }

  private ok(request: Main, content: Main["content"] = { case: "empty", value: {} as never }, hasNext = false): void {
    this.queue(create(MainSchema, {
      commandId: request.commandId,
      commandStatus: CommandStatus.OK,
      hasNext,
      content,
    }));
  }

  private respond(request: Main): void {
    switch (request.content.case) {
      case "systemPingRequest":
        this.ok(request, {
          case: "systemPingResponse",
          value: create(PingResponseSchema, { data: request.content.value.data }),
        });
        break;
      case "systemProtobufVersionRequest":
        this.ok(request, {
          case: "systemProtobufVersionResponse",
          value: create(ProtobufVersionResponseSchema, { major: 0, minor: 15 }),
        });
        break;
      case "systemDeviceInfoRequest": {
        const values = [
          ["hardware_model", "Flipper Zero"],
          ["hardware_target", "7"],
          ["firmware_version", "1.2.3"],
          ["firmware_origin_fork", "PoisonedOS"],
        ] as const;
        values.forEach(([key, value], index) => this.ok(request, {
          case: "systemDeviceInfoResponse",
          value: create(DeviceInfoResponseSchema, { key, value }),
        }, index < values.length - 1));
        break;
      }
      case "storageInfoRequest":
        this.ok(request, {
          case: "storageInfoResponse",
          value: create(InfoResponseSchema, { totalSpace: 64_000_000n, freeSpace: 32_000_000n }),
        });
        break;
      case "storageWriteRequest": {
        const stored = this.stored.get(request.content.value.path) ?? [];
        stored.push(...(request.content.value.file?.data ?? []));
        this.stored.set(request.content.value.path, stored);
        if (!request.hasNext) this.ok(request);
        break;
      }
      case "storageReadRequest":
        this.ok(request, {
          case: "storageReadResponse",
          value: create(ReadResponseSchema, {
            file: create(FileSchema, { data: Uint8Array.from(this.stored.get(request.content.value.path) ?? []) }),
          }),
        });
        break;
      case "systemUpdateRequest":
        this.ok(request, {
          case: "systemUpdateResponse",
          value: create(UpdateResponseSchema, { code: UpdateResponse_UpdateResultCode.OK }),
        });
        break;
      case "systemRebootRequest":
      case "stopSession":
        break;
      default:
        this.ok(request);
    }
  }
}

describe("Flipper browser RPC client", () => {
  it("decodes fragmented multibyte protobuf frames", () => {
    const message = create(MainSchema, {
      commandId: 19,
      content: { case: "systemPingRequest", value: { data: new Uint8Array(200), $typeName: "PB_System.PingRequest" } },
    });
    const frame = encodeDelimited(message);
    const decoder = new DelimitedMessageDecoder();
    expect(decoder.push(frame.slice(0, 1))).toEqual([]);
    expect(decoder.push(frame.slice(1, 7))).toEqual([]);
    expect(decoder.push(frame.slice(7))).toEqual([message]);
  });

  it("verifies the RPC challenge and exact device identity from streamed responses", async () => {
    const transport = new RpcFixtureTransport();
    const identity = await new FlipperRpcClient(transport).verifySession();
    expect(identity).toMatchObject({
      hardwareModel: "Flipper Zero",
      hardwareTarget: "7",
      firmwareVersion: "1.2.3",
      firmwareOrigin: "PoisonedOS",
      protobufVersion: "0.15",
    });
  });

  it("streams 512-byte writes under one command and reads bytes back for digest verification", async () => {
    const transport = new RpcFixtureTransport();
    const client = new FlipperRpcClient(transport);
    const data = new Uint8Array(1_280).map((_value, index) => index & 0xff);
    await client.writeFile("/ext/update/poison/firmware.dfu", data);
    await expect(client.verifyFile(
      "/ext/update/poison/firmware.dfu",
      await sha256(data),
      data.byteLength,
    )).resolves.toBeUndefined();
    const writes = transport.requests.filter((request) => request.content.case === "storageWriteRequest");
    expect(writes.map((request) => request.commandId)).toEqual([1, 1, 1]);
    expect(writes.map((request) => request.hasNext)).toEqual([true, true, false]);
    expect(writes.map((request) => request.content.case === "storageWriteRequest" ? request.content.value.file?.data.byteLength : 0))
      .toEqual([512, 512, 256]);
  });

  it("stops readback when the device exceeds the uploaded file size", async () => {
    const transport = new RpcFixtureTransport();
    transport.stored.set("/ext/update/poison/update.fuf", [1, 2, 3]);
    const client = new FlipperRpcClient(transport);
    await expect(client.verifyFile(
      "/ext/update/poison/update.fuf",
      await sha256(Uint8Array.from([1, 2])),
      2,
    )).rejects.toThrowError(/exceeded its expected size/);
  });

  it("prepares the real update manifest and sends reboot-to-update without waiting for a reply", async () => {
    const transport = new RpcFixtureTransport();
    const client = new FlipperRpcClient(transport);
    await client.prepareUpdate("/ext/update/poison/update.fuf");
    await client.rebootToUpdate();
    expect(transport.requests.at(-2)?.content).toMatchObject({
      case: "systemUpdateRequest",
      value: { updateManifest: "/ext/update/poison/update.fuf" },
    });
    expect(transport.requests.at(-1)?.content).toMatchObject({
      case: "systemRebootRequest",
      value: { mode: 2 },
    });
  });

  it("rejects a malformed length varint", () => {
    const decoder = new DelimitedMessageDecoder();
    expect(() => decoder.push(Uint8Array.from([0xff, 0xff, 0xff, 0xff, 0xff])))
      .toThrowError(RpcError);
  });
});

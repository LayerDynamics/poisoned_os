import { create, fromBinary, toBinary } from "@bufbuild/protobuf";
import {
  CommandStatus,
  MainSchema,
  type Main,
} from "../../dashboard/src/generated/flipper_pb";
import {
  DeleteRequestSchema,
  FileSchema,
  InfoRequestSchema,
  MkdirRequestSchema,
  ReadRequestSchema,
  WriteRequestSchema,
} from "../../dashboard/src/generated/storage_pb";
import {
  DeviceInfoRequestSchema,
  PingRequestSchema,
  ProtobufVersionRequestSchema,
  RebootRequestSchema,
  RebootRequest_RebootMode,
  UpdateRequestSchema,
  UpdateResponse_UpdateResultCode,
} from "../../dashboard/src/generated/system_pb";
import { sha256 } from "./archive";
import type { ByteTransport } from "./web-serial";

const MAX_MESSAGE_BYTES = 16_384;
const FILE_CHUNK_BYTES = 512;
const RPC_TIMEOUT_MS = 15_000;

export class RpcError extends Error {
  public constructor(message: string) {
    super(message);
    this.name = "RpcError";
  }
}

export function encodeVarint(value: number): Uint8Array {
  if (!Number.isSafeInteger(value) || value < 0) throw new RpcError("A protobuf length must be a non-negative safe integer");
  const encoded: number[] = [];
  do {
    let byte = value & 0x7f;
    value = Math.floor(value / 128);
    if (value > 0) byte |= 0x80;
    encoded.push(byte);
  } while (value > 0);
  return Uint8Array.from(encoded);
}

export function encodeDelimited(message: Main): Uint8Array {
  const payload = toBinary(MainSchema, message);
  if (payload.byteLength > MAX_MESSAGE_BYTES) throw new RpcError(`RPC message exceeds ${MAX_MESSAGE_BYTES} bytes`);
  const prefix = encodeVarint(payload.byteLength);
  const frame = new Uint8Array(prefix.byteLength + payload.byteLength);
  frame.set(prefix);
  frame.set(payload, prefix.byteLength);
  return frame;
}

export class DelimitedMessageDecoder {
  private buffer = new Uint8Array();

  public push(chunk: Uint8Array): readonly Main[] {
    if (chunk.byteLength === 0) return [];
    const combined = new Uint8Array(this.buffer.byteLength + chunk.byteLength);
    combined.set(this.buffer);
    combined.set(chunk, this.buffer.byteLength);
    this.buffer = combined;
    const messages: Main[] = [];
    let consumed = 0;
    while (consumed < this.buffer.byteLength) {
      let size = 0;
      let shift = 0;
      let prefixBytes = 0;
      let completePrefix = false;
      for (let index = consumed; index < this.buffer.byteLength && prefixBytes < 5; index += 1) {
        const byte = this.buffer[index]!;
        size += (byte & 0x7f) * (2 ** shift);
        shift += 7;
        prefixBytes += 1;
        if ((byte & 0x80) === 0) {
          completePrefix = true;
          break;
        }
      }
      if (!completePrefix) {
        if (prefixBytes >= 5) throw new RpcError("Invalid RPC length varint");
        break;
      }
      if (size > MAX_MESSAGE_BYTES) throw new RpcError(`RPC frame length ${size} exceeds ${MAX_MESSAGE_BYTES}`);
      const frameEnd = consumed + prefixBytes + size;
      if (frameEnd > this.buffer.byteLength) break;
      try {
        messages.push(fromBinary(MainSchema, this.buffer.subarray(consumed + prefixBytes, frameEnd)));
      } catch (error) {
        throw new RpcError(`Invalid RPC protobuf payload: ${error instanceof Error ? error.message : String(error)}`);
      }
      consumed = frameEnd;
    }
    if (consumed > 0) this.buffer = this.buffer.slice(consumed);
    return messages;
  }
}

export interface DeviceIdentity {
  readonly hardwareModel: string;
  readonly hardwareTarget: string;
  readonly firmwareVersion: string;
  readonly firmwareOrigin: string;
  readonly protobufVersion: string;
  readonly properties: Readonly<Record<string, string>>;
}

export interface FileProgress {
  readonly path: string;
  readonly fileBytes: number;
  readonly fileTotal: number;
}

export class FlipperRpcClient {
  private commandId = 1;
  private readonly decoder = new DelimitedMessageDecoder();
  private readonly queued: Main[] = [];
  private closed = false;

  public constructor(private readonly transport: ByteTransport) {}

  public async verifySession(signal?: AbortSignal): Promise<DeviceIdentity> {
    const challenge = crypto.getRandomValues(new Uint8Array(16));
    const ping = await this.request(create(MainSchema, {
      content: { case: "systemPingRequest", value: create(PingRequestSchema, { data: challenge }) },
    }), undefined, signal);
    if (ping.length !== 1 || ping[0]!.content.case !== "systemPingResponse" ||
        !equalBytes(ping[0]!.content.value.data, challenge)) {
      throw new RpcError("The selected device did not return the Flipper RPC challenge");
    }
    const versionResponses = await this.request(create(MainSchema, {
      content: { case: "systemProtobufVersionRequest", value: create(ProtobufVersionRequestSchema) },
    }), undefined, signal);
    if (versionResponses.length !== 1 || versionResponses[0]!.content.case !== "systemProtobufVersionResponse") {
      throw new RpcError("The selected device did not report its protobuf version");
    }
    const version = versionResponses[0]!.content.value;
    const properties = await this.deviceInfo(signal);
    return {
      hardwareModel: properties.hardware_model ?? "",
      hardwareTarget: properties.hardware_target ?? "",
      firmwareVersion: properties.firmware_version ?? "",
      firmwareOrigin: properties.firmware_origin_fork ?? "",
      protobufVersion: `${version.major}.${version.minor}`,
      properties,
    };
  }

  public async deviceInfo(signal?: AbortSignal): Promise<Record<string, string>> {
    const responses = await this.request(create(MainSchema, {
      content: { case: "systemDeviceInfoRequest", value: create(DeviceInfoRequestSchema) },
    }), undefined, signal);
    const properties: Record<string, string> = {};
    for (const response of responses) {
      if (response.content.case !== "systemDeviceInfoResponse") throw new RpcError("Flipper returned invalid device information");
      properties[response.content.value.key] = response.content.value.value;
    }
    return properties;
  }

  public async freeSpace(path = "/ext", signal?: AbortSignal): Promise<bigint> {
    const responses = await this.request(create(MainSchema, {
      content: { case: "storageInfoRequest", value: create(InfoRequestSchema, { path }) },
    }), undefined, signal);
    if (responses.length !== 1 || responses[0]!.content.case !== "storageInfoResponse") {
      throw new RpcError("Flipper did not report SD-card capacity");
    }
    return responses[0]!.content.value.freeSpace;
  }

  public async deleteTree(path: string, signal?: AbortSignal): Promise<void> {
    await this.request(create(MainSchema, {
      content: { case: "storageDeleteRequest", value: create(DeleteRequestSchema, { path, recursive: true }) },
    }), [CommandStatus.OK, CommandStatus.ERROR_STORAGE_NOT_EXIST], signal);
  }

  public async makeDirectory(path: string, signal?: AbortSignal): Promise<void> {
    await this.request(create(MainSchema, {
      content: { case: "storageMkdirRequest", value: create(MkdirRequestSchema, { path }) },
    }), [CommandStatus.OK, CommandStatus.ERROR_STORAGE_EXIST], signal);
  }

  public async makeDirectories(path: string, signal?: AbortSignal): Promise<void> {
    if (!path.startsWith("/")) throw new RpcError(`Device path must be absolute: ${path}`);
    let current = "";
    for (const part of path.split("/")) {
      if (!part) continue;
      current += `/${part}`;
      await this.makeDirectory(current, signal);
    }
  }

  public async writeFile(
    path: string,
    data: Uint8Array,
    progress?: (progress: FileProgress) => void,
    signal?: AbortSignal,
  ): Promise<void> {
    const commandId = this.allocateCommandId();
    let offset = 0;
    do {
      if (signal?.aborted) throw new RpcError("Firmware upload was cancelled");
      const end = Math.min(offset + FILE_CHUNK_BYTES, data.byteLength);
      const chunk = data.slice(offset, end);
      const final = end === data.byteLength;
      await this.transport.write(encodeDelimited(create(MainSchema, {
        commandId,
        hasNext: !final,
        content: {
          case: "storageWriteRequest",
          value: create(WriteRequestSchema, {
            path,
            file: create(FileSchema, { data: chunk }),
          }),
        },
      })), signal);
      offset = end;
      progress?.({ path, fileBytes: offset, fileTotal: data.byteLength });
    } while (offset < data.byteLength);
    await this.readResponses(commandId, [CommandStatus.OK], signal);
  }

  public async readFile(path: string, maximumBytes: number, signal?: AbortSignal): Promise<Uint8Array> {
    if (!Number.isSafeInteger(maximumBytes) || maximumBytes < 0) throw new RpcError("Expected file size is invalid");
    const commandId = this.allocateCommandId();
    await this.transport.write(encodeDelimited(create(MainSchema, {
      commandId,
      content: { case: "storageReadRequest", value: create(ReadRequestSchema, { path }) },
    })), signal);
    const chunks: Uint8Array[] = [];
    let total = 0;
    while (true) {
      const response = await this.readMessage(signal);
      if (response.commandId !== commandId) {
        throw new RpcError(`RPC response ${response.commandId} did not match command ${commandId}`);
      }
      if (response.commandStatus !== CommandStatus.OK) {
        throw new RpcError(`Flipper RPC command ${commandId} failed: ${CommandStatus[response.commandStatus] ?? response.commandStatus}`);
      }
      if (response.content.case !== "storageReadResponse") throw new RpcError(`Flipper returned invalid data while verifying ${path}`);
      const chunk = response.content.value.file?.data ?? new Uint8Array();
      total += chunk.byteLength;
      if (total > maximumBytes) throw new RpcError(`Readback for ${path} exceeded its expected size`);
      chunks.push(chunk);
      if (!response.hasNext) break;
    }
    const data = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) {
      data.set(chunk, offset);
      offset += chunk.byteLength;
    }
    return data;
  }

  public async verifyFile(path: string, expectedSha256: string, expectedBytes: number, signal?: AbortSignal): Promise<void> {
    const data = await this.readFile(path, expectedBytes, signal);
    if (data.byteLength !== expectedBytes) throw new RpcError(`Readback size did not match the upload for ${path}`);
    const actual = await sha256(data);
    if (actual !== expectedSha256) throw new RpcError(`Upload verification failed for ${path}`);
  }

  public async prepareUpdate(manifestPath: string, signal?: AbortSignal): Promise<void> {
    const responses = await this.request(create(MainSchema, {
      content: { case: "systemUpdateRequest", value: create(UpdateRequestSchema, { updateManifest: manifestPath }) },
    }), undefined, signal);
    if (responses.length !== 1 || responses[0]!.content.case !== "systemUpdateResponse") {
      throw new RpcError("Flipper returned no update preparation result");
    }
    if (responses[0]!.content.value.code !== UpdateResponse_UpdateResultCode.OK) {
      throw new RpcError(`Flipper rejected update.fuf: ${UpdateResponse_UpdateResultCode[responses[0]!.content.value.code]}`);
    }
  }

  public async rebootToUpdate(signal?: AbortSignal): Promise<void> {
    const commandId = this.allocateCommandId();
    await this.transport.write(encodeDelimited(create(MainSchema, {
      commandId,
      content: {
        case: "systemRebootRequest",
        value: create(RebootRequestSchema, { mode: RebootRequest_RebootMode.UPDATE }),
      },
    })), signal);
    this.closed = true;
  }

  public async close(): Promise<void> {
    if (!this.closed) {
      try {
        await this.transport.write(encodeDelimited(create(MainSchema, {
          commandId: this.allocateCommandId(),
          content: { case: "stopSession", value: {} },
        })));
      } catch {
        // Closing must still release the serial port after a device disconnect.
      }
    }
    this.closed = true;
    await this.transport.close();
  }

  private async request(
    message: Main,
    allowedStatuses: readonly CommandStatus[] = [CommandStatus.OK],
    signal?: AbortSignal,
  ): Promise<readonly Main[]> {
    const commandId = this.allocateCommandId();
    message.commandId = commandId;
    await this.transport.write(encodeDelimited(message), signal);
    return this.readResponses(commandId, allowedStatuses, signal);
  }

  private async readResponses(
    commandId: number,
    allowedStatuses: readonly CommandStatus[],
    signal?: AbortSignal,
  ): Promise<readonly Main[]> {
    const responses: Main[] = [];
    while (true) {
      const response = await this.readMessage(signal);
      if (response.commandId !== commandId) {
        throw new RpcError(`RPC response ${response.commandId} did not match command ${commandId}`);
      }
      if (!allowedStatuses.includes(response.commandStatus)) {
        throw new RpcError(`Flipper RPC command ${commandId} failed: ${CommandStatus[response.commandStatus] ?? response.commandStatus}`);
      }
      responses.push(response);
      if (!response.hasNext) return responses;
    }
  }

  private async readMessage(signal?: AbortSignal): Promise<Main> {
    const queued = this.queued.shift();
    if (queued) return queued;
    while (true) {
      if (signal?.aborted) throw new RpcError("RPC request was cancelled");
      const chunk = await new Promise<Uint8Array | null>((resolve, reject) => {
        const timeout = setTimeout(
          () => reject(new RpcError("Timed out waiting for the Flipper RPC response")),
          RPC_TIMEOUT_MS,
        );
        this.transport.read(signal).then(resolve, reject).finally(() => clearTimeout(timeout));
      });
      if (chunk === null) throw new RpcError("Flipper disconnected during an RPC request");
      this.queued.push(...this.decoder.push(chunk));
      const message = this.queued.shift();
      if (message) return message;
    }
  }

  private allocateCommandId(): number {
    const allocated = this.commandId;
    this.commandId = allocated === 0xffff_ffff ? 1 : allocated + 1;
    return allocated;
  }
}

function equalBytes(left: Uint8Array, right: Uint8Array): boolean {
  return left.byteLength === right.byteLength && left.every((value, index) => value === right[index]);
}

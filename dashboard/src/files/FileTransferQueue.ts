import { create } from "@bufbuild/protobuf";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import { FileTransferBeginSchema, FileTransferChunkSchema, FileTransferCompleteSchema } from "../generated/poison_files_pb";

export const FILE_CHUNK_BYTES = 512;

export interface TransferChunk { operationId: string; offset: number; data: Uint8Array; sha256: string; }
export interface TransferTransport {
  begin(operationId: string, path: string, size: number, sha256: string, signal?: AbortSignal): Promise<void>;
  send(chunk: TransferChunk, signal?: AbortSignal): Promise<void>;
  complete(operationId: string, size: number, sha256: string, signal?: AbortSignal): Promise<void>;
}
export interface TransferProgress { operationId: string; sentBytes: number; totalBytes: number; acknowledged: boolean; }

export class FileTransferError extends Error { public constructor(public readonly code: "invalid" | "checksum" | "cancelled" | "retry-exhausted", message: string) { super(message); this.name = "FileTransferError"; } }

export async function sha256Hex(data: Uint8Array): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", data as BufferSource);
  return [...new Uint8Array(digest)].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

export class FileTransferQueue {
  public constructor(private readonly transport: TransferTransport, private readonly maxRetries = 3) {}

  public async upload(operationId: string, path: string, data: Uint8Array, expectedSha256: string, onProgress?: (progress: TransferProgress) => void, signal?: AbortSignal): Promise<TransferProgress> {
    if (!/^[A-Za-z0-9._-]{1,64}$/.test(operationId) || !/^\/(?:config|profiles|apps|scripts|workloads|cases|evidence|lessons|exports)(?:\/[^/\\\0.][^/\\\0]*|\/[.][^./\\\0][^/\\\0]*)*$/.test(path) || path.length > 256 || data.byteLength > 16 * 1024 * 1024 || !/^[0-9a-f]{64}$/.test(expectedSha256)) throw new FileTransferError("invalid", "invalid transfer request");
    if (await sha256Hex(data) !== expectedSha256) throw new FileTransferError("checksum", "source checksum mismatch");
    await this.transport.begin(operationId, path, data.byteLength, expectedSha256, signal);
    let sentBytes = 0;
    for (let offset = 0; offset < data.byteLength; offset += FILE_CHUNK_BYTES) {
      if (signal?.aborted) throw new FileTransferError("cancelled", "transfer cancelled");
      const chunkData = data.slice(offset, Math.min(offset + FILE_CHUNK_BYTES, data.byteLength));
      const chunk: TransferChunk = { operationId, offset, data: chunkData, sha256: await sha256Hex(chunkData) };
      let attempt = 0;
      while (true) {
        try { await this.transport.send(chunk, signal); break; }
        catch (error) { if (++attempt > this.maxRetries) throw new FileTransferError("retry-exhausted", error instanceof Error ? error.message : String(error)); }
      }
      sentBytes += chunkData.byteLength;
      onProgress?.({ operationId, sentBytes, totalBytes: data.byteLength, acknowledged: false });
    }
    await this.transport.complete(operationId, data.byteLength, expectedSha256, signal);
    const result = { operationId, sentBytes, totalBytes: data.byteLength, acknowledged: true };
    onProgress?.(result);
    return result;
  }
}

export interface FileTransferSession {
  request(request: Main, signal?: AbortSignal): Promise<Main>;
}

export class RpcFileTransferTransport implements TransferTransport {
  private nextCommandId = 3_000;

  public constructor(private readonly session: FileTransferSession) {}

  private async request(content: Main["content"], signal?: AbortSignal): Promise<void> {
    const response = await this.session.request(create(MainSchema, {
      commandId: this.nextCommandId++,
      content,
    }), signal);
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "empty") {
      throw new Error("device rejected file transfer operation");
    }
  }

  public begin(operationId: string, path: string, size: number, sha256: string, signal?: AbortSignal): Promise<void> {
    return this.request({
      case: "poisonFileTransferBegin",
      value: create(FileTransferBeginSchema, { operationId, path, size: BigInt(size), sha256 }),
    }, signal);
  }

  public send(chunk: TransferChunk, signal?: AbortSignal): Promise<void> {
    return this.request({
      case: "poisonFileTransferChunk",
      value: create(FileTransferChunkSchema, {
        operationId: chunk.operationId,
        offset: BigInt(chunk.offset),
        data: chunk.data,
        sha256: chunk.sha256,
      }),
    }, signal);
  }

  public complete(operationId: string, size: number, sha256: string, signal?: AbortSignal): Promise<void> {
    return this.request({
      case: "poisonFileTransferComplete",
      value: create(FileTransferCompleteSchema, { operationId, size: BigInt(size), sha256 }),
    }, signal);
  }
}

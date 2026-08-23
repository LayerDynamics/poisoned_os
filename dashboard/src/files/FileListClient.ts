import { create } from "@bufbuild/protobuf";
import { MainSchema, type Main } from "../generated/flipper_pb";
import { FileListRequestSchema, type FileStat } from "../generated/poison_files_pb";

export interface FileListSession {
  requestStream(request: Main, maxResponses?: number, signal?: AbortSignal): Promise<readonly Main[]>;
}

export interface FileListPage {
  readonly entries: readonly FileStat[];
  readonly nextCursor: string;
}

export class FileListClient {
  private nextCommandId = 4_000;

  public constructor(private readonly session: FileListSession) {}

  public async list(path: string, cursor = "", pageSize = 16, signal?: AbortSignal): Promise<FileListPage> {
    if (!/^\/(?:system|config|profiles|apps|scripts|workloads|cases|evidence|lessons|exports|int|ext)(?:\/[^/\\\0.][^/\\\0]*|\/[.][^./\\\0][^/\\\0]*)*$/.test(path) ||
        path.length > 256 || !/^\d*$/.test(cursor) || cursor.length > 128 ||
        !Number.isInteger(pageSize) || pageSize < 1 || pageSize > 16) {
      throw new Error("file list request is outside its bounds");
    }
    const request = create(MainSchema, {
      commandId: this.nextCommandId++,
      content: {
        case: "poisonFileListRequest",
        value: create(FileListRequestSchema, { path, cursor, pageSize }),
      },
    });
    const responses = await this.session.requestStream(request, pageSize, signal);
    const entries: FileStat[] = [];
    let nextCursor = "";
    for (const [index, response] of responses.entries()) {
      if (response.content.case !== "poisonFileListResponse" || response.content.value.entries.length > 1 ||
          (response.content.value.nextCursor && index + 1 !== responses.length)) {
        throw new Error("device returned an invalid file list page");
      }
      for (const entry of response.content.value.entries) {
        const prefix = path.endsWith("/") ? path : `${path}/`;
        if (!entry.path.startsWith(prefix) || entry.path.slice(prefix.length).includes("/") ||
            entry.size < 0n || entry.size > BigInt(Number.MAX_SAFE_INTEGER) ||
            entry.modifiedAtMs < 0n || entry.modifiedAtMs > BigInt(Number.MAX_SAFE_INTEGER) ||
            (entry.sha256 !== "" && !/^[0-9a-f]{64}$/.test(entry.sha256))) {
          throw new Error("device returned an invalid file entry");
        }
        entries.push(entry);
      }
      nextCursor = response.content.value.nextCursor;
    }
    if (nextCursor && !/^\d{1,128}$/.test(nextCursor)) {
      throw new Error("device returned an invalid file cursor");
    }
    return { entries, nextCursor };
  }
}

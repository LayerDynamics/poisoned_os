import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { CommandStatus, MainSchema } from "../generated/flipper_pb";
import { FileListResponseSchema, FileStatSchema } from "../generated/poison_files_pb";
import { FileListClient } from "./FileListClient";

describe("FileListClient", () => {
  it("lists logical paths through the generated streamed RPC response", async () => {
    const client = new FileListClient({
      async requestStream(request) {
        expect(request.content.case).toBe("poisonFileListRequest");
        return [create(MainSchema, {
          commandId: request.commandId,
          commandStatus: CommandStatus.OK,
          content: {
            case: "poisonFileListResponse",
            value: create(FileListResponseSchema, {
              entries: [create(FileStatSchema, { path: "/workloads/app.mjs", size: 7n })],
            }),
          },
        })];
      },
    });
    const page = await client.list("/workloads");
    expect(page.entries.map((entry) => entry.path)).toEqual(["/workloads/app.mjs"]);
  });
});

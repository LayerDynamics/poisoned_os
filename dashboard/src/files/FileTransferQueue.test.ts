import { describe, expect, it } from "vitest";
import { create } from "@bufbuild/protobuf";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import { FileTransferQueue, RpcFileTransferTransport, sha256Hex } from "./FileTransferQueue";

describe("FileTransferQueue", () => {
  it("chunks, retries idempotently, and acknowledges only after completion", async () => {
    const sent: number[] = []; let failures = 0; const transport = { begin: async () => {}, send: async (chunk: { offset: number }) => { if (failures++ === 0) throw new Error("retry"); sent.push(chunk.offset); }, complete: async () => {} };
    const data = new TextEncoder().encode("transfer"); const queue = new FileTransferQueue(transport, 2); const result = await queue.upload("op", "/workloads/transfer.bin", data, await sha256Hex(data));
    expect(sent).toEqual([0]); expect(result.acknowledged).toBe(true);
  });

  it("sends begin, bounded chunks, and completion through generated PB_Main operations", async () => {
    const requests: Main[] = [];
    const transport = new RpcFileTransferTransport({
      async request(request) {
        requests.push(request);
        return create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, content: { case: "empty", value: {} } });
      },
    });
    const data = new Uint8Array(513).fill(7);
    await new FileTransferQueue(transport).upload("rpc-1", "/workloads/rpc.bin", data, await sha256Hex(data));
    expect(requests.map((request) => request.content.case)).toEqual([
      "poisonFileTransferBegin",
      "poisonFileTransferChunk",
      "poisonFileTransferChunk",
      "poisonFileTransferComplete",
    ]);
    expect(requests[1].content.case === "poisonFileTransferChunk" && requests[1].content.value.data.byteLength).toBe(512);
  });

  it("creates deterministic empty source files without manufacturing a data chunk", async () => {
    const operations: string[] = [];
    const queue = new FileTransferQueue({
      async begin() { operations.push("begin"); },
      async send() { operations.push("chunk"); },
      async complete() { operations.push("complete"); },
    });
    const empty = new Uint8Array();
    await queue.upload("empty-js", "/scripts/project/empty.js", empty, await sha256Hex(empty));
    expect(operations).toEqual(["begin", "complete"]);
  });
});

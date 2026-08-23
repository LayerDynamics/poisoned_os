import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { AppEventSchema } from "../generated/poison_app_pb";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import { StructuredAppClient, type StructuredAppSession } from "./StructuredAppClient";

describe("StructuredAppClient", () => {
  it("rejects event gaps and supports cancellation", () => {
    const client = new StructuredAppClient();
    client.start("run", 2);
    client.event({ runId: "run", sequence: 0n, timeMs: 1, level: "info", kind: "status", message: "started", artifactIds: [] });
    expect(() => client.event({ runId: "run", sequence: 2n, timeMs: 2, level: "info", kind: "result", message: "done", artifactIds: [] })).toThrow();
    expect(client.cancel().cancelled).toBe(true);
  });

  it("sends the generated protobuf command and consumes encrypted-session notifications", async () => {
    let notify: ((message: Main) => void) | undefined;
    const session: StructuredAppSession = {
      onNotification(handler) { notify = handler; return () => { notify = undefined; }; },
      async request(request) {
        expect(request.content.case).toBe("poisonAppCommand");
        if (request.content.case !== "poisonAppCommand") throw new Error("wrong request");
        expect(request.content.value).toMatchObject({
          appId: "poison_safe_sample",
          runId: "onboarding",
          commandId: "run",
          payloadJson: "",
          protocolVersion: 1,
          chunkIndex: 0,
          chunkCount: 1,
        });
        expect(new TextDecoder().decode(request.content.value.payloadChunk)).toBe("2");
        notify?.(create(MainSchema, {
          commandId: 0,
          commandStatus: CommandStatus.OK,
          content: { case: "poisonAppEvent", value: create(AppEventSchema, {
            appId: "poison_safe_sample",
            runId: "onboarding",
            eventId: "progress-start",
            sequence: 0n,
            protocolVersion: 1,
            kind: { case: "progress", value: { percent: 25, label: "running", $typeName: "PB_Poison.AppProgress" } },
          }) },
        }));
        return create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, content: { case: "empty", value: {} } });
      },
    };
    const client = new StructuredAppClient(session, () => 7);
    client.start("onboarding", 2);

    await client.command("poison_safe_sample", "run", "2");

    expect(client.current?.events).toEqual([expect.objectContaining({
      runId: "onboarding",
      eventId: "progress-start",
      sequence: 0n,
      timeMs: 7,
      progressPercent: 25,
      message: "running",
    })]);
    client.dispose();
  });

  it("launches the real firmware application before structured commands", async () => {
    const session: StructuredAppSession = {
      onNotification() { return () => undefined; },
      async request(request) {
        expect(request.content.case).toBe("appStartRequest");
        if (request.content.case !== "appStartRequest") throw new Error("wrong request");
        expect(request.content.value).toMatchObject({ name: "Poison Safe Sample", args: "" });
        return create(MainSchema, {
          commandId: request.commandId,
          commandStatus: CommandStatus.OK,
          content: { case: "empty", value: {} },
        });
      },
    };
    await expect(new StructuredAppClient(session).launchApplication("Poison Safe Sample")).resolves.toBeUndefined();
  });

  it("chunks large commands and reassembles typed table events", async () => {
    let notify: ((message: Main) => void) | undefined;
    const payloadParts: Uint8Array[] = [];
    const schema = new TextEncoder().encode('{"columns":["value"]}');
    const rows = new TextEncoder().encode('[["complete"]]');
    const session: StructuredAppSession = {
      onNotification(handler) { notify = handler; return () => { notify = undefined; }; },
      async request(request) {
        if (request.content.case !== "poisonAppCommand") throw new Error("wrong request");
        payloadParts.push(request.content.value.payloadChunk);
        if (request.content.value.chunkIndex === request.content.value.chunkCount - 1) {
          const eventChunks = [[schema, new Uint8Array()], [new Uint8Array(), rows]] as const;
          for (const [index, chunks] of eventChunks.entries()) {
            notify?.(create(MainSchema, {
              commandId: 0,
              commandStatus: CommandStatus.OK,
              content: { case: "poisonAppEvent", value: create(AppEventSchema, {
                appId: "poison_safe_sample",
                runId: "large-run",
                eventId: "table-0",
                sequence: 0n,
                protocolVersion: 1,
                chunkIndex: index,
                chunkCount: 2,
                schemaJsonChunk: chunks[0],
                rowsJsonChunk: chunks[1],
                kind: { case: "table", value: { schemaJson: "", rowsJson: "", $typeName: "PB_Poison.AppTable" } },
              }) },
            }));
          }
        }
        return create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, content: { case: "empty", value: {} } });
      },
    };
    const client = new StructuredAppClient(session, () => 9);
    const payload = "x".repeat(900);
    client.start("large-run", 1);

    await client.command("poison_safe_sample", "run", payload);

    const joined = new Uint8Array(payloadParts.reduce((total, part) => total + part.byteLength, 0));
    let offset = 0;
    for (const part of payloadParts) { joined.set(part, offset); offset += part.byteLength; }
    expect(new TextDecoder().decode(joined)).toBe(payload);
    expect(payloadParts.map((part) => part.byteLength)).toEqual([384, 384, 132]);
    expect(client.current?.events).toEqual([expect.objectContaining({
      kind: "table",
      schemaJson: '{"columns":["value"]}',
      rowsJson: '[["complete"]]',
    })]);
  });
});

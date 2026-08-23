import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { AppEventSchema } from "../generated/poison_app_pb";
import { CommandStatus, EmptySchema, MainSchema, type Main } from "../generated/flipper_pb";
import { ToolClient } from "./ToolClient";
import { CancelledSchema } from "../generated/poison_session_pb";

describe("ToolClient", () => {
  it("starts a tool and cancels its registered device operation by RPC command id", async () => {
    const states: string[] = [];
    const client = new ToolClient({
      async request(request: Main) {
        if (request.content.case === "poisonToolRun") {
          states.push(request.content.value.state);
          return create(MainSchema, {
            commandId: request.commandId,
            commandStatus: CommandStatus.OK,
            content: { case: "empty", value: create(EmptySchema) },
          });
        }
        expect(request.content.case).toBe("poisonCancelRequest");
        if (request.content.case !== "poisonCancelRequest") throw new Error("wrong request");
        expect(request.content.value.commandId).toBe(20_000n);
        states.push("cancel");
        return create(MainSchema, {
          commandId: request.commandId,
          commandStatus: CommandStatus.OK,
          content: {
            case: "poisonCancelled",
            value: create(CancelledSchema, { commandId: 20_000n, accepted: true }),
          },
        });
      },
      onNotification() { return () => undefined; },
    });
    await client.start("nfc.read", "run-1");
    await client.stop();
    expect(states).toEqual(["start", "cancel"]);
    expect(client.current?.active).toBe(false);
  });

  it("rejects unbounded identifiers before transport", async () => {
    const client = new ToolClient({
      async request() { throw new Error("transport must not be called"); },
      onNotification() { return () => undefined; },
    });
    await expect(client.start("NFC READ", "run-1")).rejects.toThrow("tool id is invalid");
  });

  it("delivers a device result that arrives after command acknowledgement", async () => {
    let notify: ((message: Main) => void) | undefined;
    const client = new ToolClient({
      async request(request: Main) {
        return create(MainSchema, {
          commandId: request.commandId,
          commandStatus: CommandStatus.OK,
          content: { case: "empty", value: create(EmptySchema) },
        });
      },
      onNotification(handler) { notify = handler; return () => { notify = undefined; }; },
    });
    const observed: number[] = [];
    client.subscribe((state) => observed.push(state.events.length));
    await client.start("usb-hid.inspect", "run-async");
    await client.command("usb-hid.status", {});

    notify?.(create(MainSchema, {
      commandId: 0,
      commandStatus: CommandStatus.OK,
      content: { case: "poisonAppEvent", value: create(AppEventSchema, {
        appId: "org.poison.tools",
        runId: "run-async",
        eventId: "run-async-0",
        sequence: 0n,
        protocolVersion: 1,
        chunkIndex: 0,
        chunkCount: 1,
        messageChunk: new TextEncoder().encode('{"connected":true}'),
        kind: { case: "result", value: { success: true, message: "", $typeName: "PB_Poison.AppResult" } },
      }) },
    }));

    expect(client.current?.events).toEqual([
      expect.objectContaining({ kind: "result", message: '{"connected":true}' }),
    ]);
    expect(observed.at(-1)).toBe(1);
  });
});

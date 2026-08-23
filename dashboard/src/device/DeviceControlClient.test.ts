import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import { ScreenFrameSchema, ScreenOrientation } from "../generated/gui_pb";
import { PingResponseSchema } from "../generated/system_pb";
import { DeviceControlClient, type DeviceControlSession } from "./DeviceControlClient";
import type { ScreenFrame } from "./RemoteScreen";

describe("DeviceControlClient", () => {
  it("streams encrypted PB_Main frames and sends real input and app lifecycle requests", async () => {
    const requests: string[] = [];
    let notification: ((message: Main) => void) | null = null;
    let frameResolve!: (frame: ScreenFrame) => void;
    const frameReceived = new Promise<ScreenFrame>((resolve) => { frameResolve = resolve; });
    let emitted = false;
    const session: DeviceControlSession = {
      onNotification(handler) {
        notification = handler;
        return () => { notification = null; };
      },
      async request(request) {
        requests.push(request.content.case ?? "missing");
        if (request.content.case === "systemPingRequest") {
          if (!emitted) {
            emitted = true;
            notification?.(create(MainSchema, {
              commandStatus: CommandStatus.OK,
              content: {
                case: "guiScreenFrame",
                value: create(ScreenFrameSchema, {
                  data: new Uint8Array(1024).fill(0x5a),
                  orientation: ScreenOrientation.HORIZONTAL,
                }),
              },
            }));
          }
          return create(MainSchema, {
            commandId: request.commandId,
            commandStatus: CommandStatus.OK,
            content: {
              case: "systemPingResponse",
              value: create(PingResponseSchema, { data: new Uint8Array([0x50]) }),
            },
          });
        }
        return create(MainSchema, {
          commandId: request.commandId,
          commandStatus: CommandStatus.OK,
          content: { case: "empty", value: {} },
        });
      },
    };
    const appStates: string[] = [];
    const client = new DeviceControlClient(session, {
      onFrame: frameResolve,
      onAppState: (state) => appStates.push(state),
    }, () => 7);

    await client.startScreenStream();
    const frame = await frameReceived;
    expect(frame.data.byteLength).toBe(1024);
    expect(frame.receivedAtMs).toBe(7);
    const input = client.inputController();
    await input.short("ok", 8);
    await client.launchApp("NFC", "");
    await client.exitApp();
    await client.stopScreenStream();
    await client.dispose();

    expect(requests).toContain("guiStartScreenStreamRequest");
    expect(requests).toContain("guiSendInputEventRequest");
    expect(requests).toContain("appStartRequest");
    expect(requests).toContain("appExitRequest");
    expect(requests).toContain("guiStopScreenStreamRequest");
    expect(appStates).toEqual(["started", "closed"]);
  });
});

import { create } from "@bufbuild/protobuf";
import { AppExitRequestSchema, StartRequestSchema } from "../generated/application_pb";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import {
  InputKey as WireInputKey,
  InputType as WireInputType,
  ScreenOrientation as WireScreenOrientation,
  SendInputEventRequestSchema,
  StartScreenStreamRequestSchema,
  StopScreenStreamRequestSchema,
} from "../generated/gui_pb";
import { PingRequestSchema } from "../generated/system_pb";
import { InputController, type InputEvent, type InputKey, type InputType } from "./InputController";
import { acceptScreenFrame, type ScreenFrame, type ScreenOrientation } from "./RemoteScreen";

export interface DeviceControlSession {
  request(request: Main, signal?: AbortSignal): Promise<Main>;
  onNotification(handler: (message: Main) => void): () => void;
}

export interface DeviceControlCallbacks {
  readonly onFrame: (frame: ScreenFrame) => void;
  readonly onAppState?: (state: "started" | "closed") => void;
  readonly onError?: (error: Error) => void;
}

const keyMap: Readonly<Record<InputKey, WireInputKey>> = {
  up: WireInputKey.UP,
  down: WireInputKey.DOWN,
  left: WireInputKey.LEFT,
  right: WireInputKey.RIGHT,
  ok: WireInputKey.OK,
  back: WireInputKey.BACK,
};

const typeMap: Readonly<Record<InputType, WireInputType>> = {
  press: WireInputType.PRESS,
  release: WireInputType.RELEASE,
  short: WireInputType.SHORT,
  long: WireInputType.LONG,
  repeat: WireInputType.REPEAT,
};

const orientationMap: Readonly<Record<number, ScreenOrientation>> = {
  [WireScreenOrientation.HORIZONTAL]: "horizontal",
  [WireScreenOrientation.HORIZONTAL_FLIP]: "horizontal-flip",
  [WireScreenOrientation.VERTICAL]: "vertical",
  [WireScreenOrientation.VERTICAL_FLIP]: "vertical-flip",
};

export class DeviceControlClient {
  private nextCommandId = 20_000;
  private frameSequence = 0n;
  private previousFrame: ScreenFrame | null = null;
  private streaming = false;
  private streamStarted = false;
  private disposed = false;
  private pumpPromise: Promise<void> | null = null;
  private readonly unsubscribe: () => void;

  public constructor(
    private readonly session: DeviceControlSession,
    private readonly callbacks: DeviceControlCallbacks,
    private readonly now: () => number = Date.now,
  ) {
    this.unsubscribe = session.onNotification((message) => this.notification(message));
  }

  public inputController(): InputController {
    return new InputController((event) => this.sendInput(event));
  }

  public async startScreenStream(signal?: AbortSignal): Promise<void> {
    if (this.disposed) throw new Error("device control client is disposed");
    if (this.streamStarted) return;
    const response = await this.session.request(create(MainSchema, {
      commandId: this.reserveCommandId(),
      content: {
        case: "guiStartScreenStreamRequest",
        value: create(StartScreenStreamRequestSchema),
      },
    }), signal);
    this.requireEmpty(response, "device rejected screen streaming");
    this.streamStarted = true;
    if (this.disposed) {
      await this.stopScreenStream(signal);
      return;
    }
    this.streaming = true;
    this.pumpPromise = this.pumpNotifications();
  }

  public async stopScreenStream(signal?: AbortSignal): Promise<void> {
    if (!this.streamStarted) return;
    this.streaming = false;
    await this.pumpPromise;
    this.pumpPromise = null;
    const response = await this.session.request(create(MainSchema, {
      commandId: this.reserveCommandId(),
      content: {
        case: "guiStopScreenStreamRequest",
        value: create(StopScreenStreamRequestSchema),
      },
    }), signal);
    this.requireEmpty(response, "device rejected stopping the screen stream");
    this.streamStarted = false;
  }

  public async launchApp(name: string, args = "", signal?: AbortSignal): Promise<void> {
    const encoder = new TextEncoder();
    if (!name || encoder.encode(name).byteLength > 512 || encoder.encode(args).byteLength > 512 ||
        name.includes("\0") || args.includes("\0")) {
      throw new Error("application request is outside its bounds");
    }
    const response = await this.session.request(create(MainSchema, {
      commandId: this.reserveCommandId(),
      content: {
        case: "appStartRequest",
        value: create(StartRequestSchema, { name, args }),
      },
    }), signal);
    this.requireEmpty(response, "device rejected application launch");
    this.callbacks.onAppState?.("started");
  }

  public async exitApp(signal?: AbortSignal): Promise<void> {
    const response = await this.session.request(create(MainSchema, {
      commandId: this.reserveCommandId(),
      content: {
        case: "appExitRequest",
        value: create(AppExitRequestSchema),
      },
    }), signal);
    this.requireEmpty(response, "device rejected application exit");
    this.callbacks.onAppState?.("closed");
  }

  public async dispose(): Promise<void> {
    if (this.disposed) return;
    this.disposed = true;
    try {
      await this.stopScreenStream();
    } finally {
      this.unsubscribe();
    }
  }

  private async sendInput(event: InputEvent): Promise<void> {
    const response = await this.session.request(create(MainSchema, {
      commandId: this.reserveCommandId(),
      content: {
        case: "guiSendInputEventRequest",
        value: create(SendInputEventRequestSchema, {
          key: keyMap[event.key],
          type: typeMap[event.type],
        }),
      },
    }));
    this.requireEmpty(response, "device rejected input event");
  }

  private async pumpNotifications(): Promise<void> {
    try {
      while (this.streaming) {
        const response = await this.session.request(create(MainSchema, {
          commandId: this.reserveCommandId(),
          content: {
            case: "systemPingRequest",
            value: create(PingRequestSchema, { data: new Uint8Array([0x50]) }),
          },
        }));
        if (response.commandStatus !== CommandStatus.OK || response.content.case !== "systemPingResponse" ||
            response.content.value.data.byteLength !== 1 || response.content.value.data[0] !== 0x50) {
          throw new Error("device notification pump failed");
        }
        await new Promise((resolve) => setTimeout(resolve, 25));
      }
    } catch (error) {
      this.streaming = false;
      this.callbacks.onError?.(error instanceof Error ? error : new Error(String(error)));
    }
  }

  private notification(message: Main): void {
    if (message.commandId !== 0 || message.commandStatus !== CommandStatus.OK) return;
    if (message.content.case === "guiScreenFrame") {
      const orientation = orientationMap[message.content.value.orientation];
      if (!orientation) {
        this.callbacks.onError?.(new Error("device returned an invalid screen orientation"));
        return;
      }
      try {
        const frame = acceptScreenFrame(this.previousFrame, {
          sequence: this.frameSequence++,
          data: message.content.value.data.slice(),
          orientation,
          receivedAtMs: this.now(),
        });
        this.previousFrame = frame;
        this.callbacks.onFrame(frame);
      } catch (error) {
        this.callbacks.onError?.(error instanceof Error ? error : new Error(String(error)));
      }
    } else if (message.content.case === "appStateResponse") {
      this.callbacks.onAppState?.(message.content.value.state === 0 ? "closed" : "started");
    }
  }

  private requireEmpty(response: Main, message: string): void {
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "empty") {
      throw new Error(message);
    }
  }

  private reserveCommandId(): number {
    const commandId = this.nextCommandId;
    this.nextCommandId = this.nextCommandId === 0xffffffff ? 20_000 : this.nextCommandId + 1;
    return commandId;
  }
}

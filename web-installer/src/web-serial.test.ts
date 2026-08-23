import { afterEach, describe, expect, it, vi } from "vitest";
import {
  FLIPPER_RUNTIME_PRODUCT_ID,
  FLIPPER_USB_VENDOR_ID,
  SerialConnectionError,
  WebSerialConnection,
  waitForAuthorizedFlipper,
  type SerialPortLike,
} from "./web-serial";

class FakePort extends EventTarget implements SerialPortLike {
  public readable: ReadableStream<Uint8Array> | null = null;
  public writable: WritableStream<Uint8Array> | null = null;
  public readonly writes: string[] = [];
  public baudRate = 0;
  private controller: ReadableStreamDefaultController<Uint8Array> | null = null;

  public constructor(private readonly productId = FLIPPER_RUNTIME_PRODUCT_ID) {
    super();
  }

  public async open(options: { baudRate: number }): Promise<void> {
    this.baudRate = options.baudRate;
    this.readable = new ReadableStream<Uint8Array>({
      start: (controller) => { this.controller = controller; },
    });
    this.writable = new WritableStream<Uint8Array>({
      write: (data) => {
        const value = new TextDecoder().decode(data);
        this.writes.push(value);
        if (value === "\r") this.controller?.enqueue(new TextEncoder().encode("\r\n>: "));
        if (value === "start_rpc_session\r") this.controller?.enqueue(new TextEncoder().encode("start_rpc_session\r\n"));
      },
    });
  }

  public async close(): Promise<void> {
    this.readable = null;
    this.writable = null;
    this.controller = null;
  }

  public enqueue(data: Uint8Array): void {
    this.controller?.enqueue(data);
  }

  public getInfo(): { usbVendorId: number; usbProductId: number } {
    return { usbVendorId: FLIPPER_USB_VENDOR_ID, usbProductId: this.productId };
  }
}

const originalWindow = Object.getOwnPropertyDescriptor(globalThis, "window");
const originalNavigator = Object.getOwnPropertyDescriptor(globalThis, "navigator");

afterEach(() => {
  if (originalWindow) Object.defineProperty(globalThis, "window", originalWindow);
  else Reflect.deleteProperty(globalThis, "window");
  if (originalNavigator) Object.defineProperty(globalThis, "navigator", originalNavigator);
  else Reflect.deleteProperty(globalThis, "navigator");
});

describe("Flipper Web Serial connection", () => {
  it("opens only the Flipper CDC identity and enters CLI RPC mode", async () => {
    const port = new FakePort();
    const connection = new WebSerialConnection(port);
    await connection.connect();
    expect(port.baudRate).toBe(230_400);
    expect(port.writes).toEqual(["\r", "start_rpc_session\r"]);
    await connection.close();
  });

  it("rejects a USB device with the wrong runtime product id", () => {
    expect(() => new WebSerialConnection(new FakePort(0xdf11)))
      .toThrowError(/not a Flipper Zero runtime/);
  });

  it("requests a browser-approved port with the exact Flipper filter", async () => {
    const port = new FakePort();
    let options: unknown;
    Object.defineProperty(globalThis, "window", { configurable: true, value: { isSecureContext: true } });
    Object.defineProperty(globalThis, "navigator", {
      configurable: true,
      value: {
        serial: Object.assign(new EventTarget(), {
          async getPorts() { return [port]; },
          async requestPort(value: unknown) { options = value; return port; },
        }),
      },
    });
    const connection = await WebSerialConnection.request();
    expect(connection.port).toBe(port);
    expect(options).toEqual({
      filters: [{ usbVendorId: FLIPPER_USB_VENDOR_ID, usbProductId: FLIPPER_RUNTIME_PRODUCT_ID }],
    });
  });

  it("requires a secure browser context", async () => {
    Object.defineProperty(globalThis, "window", { configurable: true, value: { isSecureContext: false } });
    Object.defineProperty(globalThis, "navigator", { configurable: true, value: {} });
    await expect(WebSerialConnection.request()).rejects.toBeInstanceOf(SerialConnectionError);
  });

  it("removes the abort listener after a completed serial read", async () => {
    const port = new FakePort();
    const connection = new WebSerialConnection(port);
    await connection.connect();
    const controller = new AbortController();
    const remove = vi.spyOn(controller.signal, "removeEventListener");
    port.enqueue(Uint8Array.from([7, 8, 9]));
    await expect(connection.read(controller.signal)).resolves.toEqual(Uint8Array.from([7, 8, 9]));
    expect(remove).toHaveBeenCalledWith("abort", expect.any(Function));
    await connection.close();
  });

  it("cancels reconnect waiting and removes its abort listener", async () => {
    const port = new FakePort();
    Object.defineProperty(globalThis, "navigator", {
      configurable: true,
      value: {
        serial: Object.assign(new EventTarget(), {
          async getPorts() { return []; },
          async requestPort() { return port; },
        }),
      },
    });
    const controller = new AbortController();
    const remove = vi.spyOn(controller.signal, "removeEventListener");
    const waiting = waitForAuthorizedFlipper(port, 10_000, undefined, controller.signal);
    controller.abort();
    await expect(waiting).rejects.toThrowError(/cancelled/);
    expect(remove).toHaveBeenCalledWith("abort", expect.any(Function));
  });
});

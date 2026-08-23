import { afterEach, beforeEach, describe, expect, it } from "vitest";
import {
  FLIPPER_BLE_SERIAL_UUIDS,
  WebBluetoothTransport,
} from "./WebBluetoothTransport";
import { WebUsbTransport } from "./WebUsbTransport";
import { WebSerialTransport } from "./WebSerialTransport";

function dataView(bytes: readonly number[]): DataView {
  const value = Uint8Array.from(bytes);
  return new DataView(value.buffer);
}

function flowCredit(value: number): DataView {
  const bytes = new Uint8Array(4);
  new DataView(bytes.buffer).setUint32(0, value, false);
  return new DataView(bytes.buffer);
}

class FakeCharacteristic extends EventTarget {
  public value: DataView | null;
  public readonly writes: Uint8Array[] = [];
  public notifying = false;

  public constructor(initialValue: DataView | null = null) {
    super();
    this.value = initialValue;
  }

  public async readValue(): Promise<DataView> {
    if (!this.value) throw new Error("no characteristic value");
    return this.value;
  }

  public async startNotifications(): Promise<this> {
    this.notifying = true;
    return this;
  }

  public async stopNotifications(): Promise<this> {
    this.notifying = false;
    return this;
  }

  public async writeValueWithoutResponse(value: BufferSource): Promise<void> {
    const bytes = value instanceof ArrayBuffer
      ? new Uint8Array(value)
      : new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    this.writes.push(Uint8Array.from(bytes));
  }

  public async writeValueWithResponse(value: BufferSource): Promise<void> {
    await this.writeValueWithoutResponse(value);
  }

  public notify(value: DataView): void {
    this.value = value;
    this.dispatchEvent(new Event("characteristicvaluechanged"));
  }
}

class FakeService {
  public constructor(private readonly characteristics: ReadonlyMap<string, FakeCharacteristic>) {}

  public async getCharacteristic(uuid: string): Promise<FakeCharacteristic> {
    const characteristic = this.characteristics.get(uuid);
    if (!characteristic) throw new Error(`missing characteristic ${uuid}`);
    return characteristic;
  }
}

class FakeServer {
  public connected = false;

  public constructor(private readonly service: FakeService) {}

  public async connect(): Promise<this> {
    this.connected = true;
    return this;
  }

  public disconnect(): void {
    this.connected = false;
  }

  public async getPrimaryService(uuid: string): Promise<FakeService> {
    if (uuid !== FLIPPER_BLE_SERIAL_UUIDS.service) throw new Error("wrong service UUID");
    return this.service;
  }
}

class FakeDevice extends EventTarget {
  public readonly id = "flipper-ble-1";
  public readonly name = "Poisoned Flipper";

  public constructor(public readonly gatt: FakeServer) {
    super();
  }
}

describe("browser device transports", () => {
  let navigatorDescriptor: PropertyDescriptor | undefined;
  let tx: FakeCharacteristic;
  let rx: FakeCharacteristic;
  let flow: FakeCharacteristic;
  let status: FakeCharacteristic;
  let server: FakeServer;
  let requestOptions: unknown;

  beforeEach(() => {
    navigatorDescriptor = Object.getOwnPropertyDescriptor(globalThis, "navigator");
    tx = new FakeCharacteristic();
    rx = new FakeCharacteristic();
    flow = new FakeCharacteristic(flowCredit(486));
    status = new FakeCharacteristic(dataView([0, 0, 0, 0]));
    const service = new FakeService(new Map([
      [FLIPPER_BLE_SERIAL_UUIDS.tx, tx],
      [FLIPPER_BLE_SERIAL_UUIDS.rx, rx],
      [FLIPPER_BLE_SERIAL_UUIDS.flowControl, flow],
      [FLIPPER_BLE_SERIAL_UUIDS.rpcStatus, status],
    ]));
    server = new FakeServer(service);
    const device = new FakeDevice(server);
    Object.defineProperty(globalThis, "navigator", {
      configurable: true,
      value: {
        bluetooth: {
          requestDevice: async (options: unknown) => {
            requestOptions = options;
            return device;
          },
        },
      },
    });
  });

  afterEach(() => {
    if (navigatorDescriptor) {
      Object.defineProperty(globalThis, "navigator", navigatorDescriptor);
    } else {
      Reflect.deleteProperty(globalThis, "navigator");
    }
  });

  it("selects only the firmware serial service and opens the real RPC characteristics", async () => {
    const transport = new WebBluetoothTransport();
    const [device] = await transport.discover();
    expect(requestOptions).toEqual({
      filters: [{ services: [FLIPPER_BLE_SERIAL_UUIDS.service] }],
    });
    expect(device).toMatchObject({
      id: "flipper-ble-1",
      label: "Poisoned Flipper",
      kind: "bluetooth",
    });

    await transport.connect(device!);
    expect(transport.health).toMatchObject({ connected: true, writable: true });
    expect(tx.notifying).toBe(true);
    expect(flow.notifying).toBe(true);
    expect(status.writes).toEqual([Uint8Array.from([1, 0, 0, 0])]);

    await transport.close();
    expect(status.writes).toEqual([
      Uint8Array.from([1, 0, 0, 0]),
      Uint8Array.from([0, 0, 0, 0]),
    ]);
    expect(server.connected).toBe(false);
    expect(transport.health.connected).toBe(false);
  });

  it("chunks writes at the firmware limit and waits for renewed flow credit", async () => {
    flow.value = flowCredit(243);
    const transport = new WebBluetoothTransport();
    const [device] = await transport.discover();
    await transport.connect(device!);

    const write = transport.write(new Uint8Array(300).fill(0xa5));
    await Promise.resolve();
    await Promise.resolve();
    expect(rx.writes).toHaveLength(1);
    expect(rx.writes[0]).toHaveLength(243);

    flow.notify(flowCredit(486));
    await write;
    expect(rx.writes.map((chunk) => chunk.byteLength)).toEqual([243, 57]);
    expect(transport.health.queuedBytes).toBe(0);
  });

  it("delivers TX indications and removes an aborted pending read", async () => {
    const transport = new WebBluetoothTransport();
    const [device] = await transport.discover();
    await transport.connect(device!);

    const abort = new AbortController();
    const abandoned = transport.read(abort.signal);
    abort.abort();
    await expect(abandoned).rejects.toMatchObject({ code: "aborted" });

    const received = transport.read();
    tx.notify(dataView([1, 2, 3, 4]));
    await expect(received).resolves.toEqual(Uint8Array.from([1, 2, 3, 4]));
  });

  it("rejects WebUSB instead of reporting a connection that discards RPC frames", async () => {
    const transport = new WebUsbTransport();
    await expect(transport.discover()).rejects.toMatchObject({ code: "unsupported" });
    await expect(transport.connect({
      id: "usb-0",
      label: "not-a-runtime-channel",
      kind: "usb",
      metadata: {},
    })).rejects.toMatchObject({ code: "unsupported" });
    await expect(transport.write(Uint8Array.from([1]))).rejects.toMatchObject({ code: "not-connected" });
    expect(transport.health).toMatchObject({ connected: false, writable: false });
  });

  it("enters Flipper CLI RPC mode before exposing USB serial to SessionClient", async () => {
    const writes: Uint8Array[] = [];
    let readableController: ReadableStreamDefaultController<Uint8Array>;
    const port = Object.assign(new EventTarget(), {
      readable: new ReadableStream<Uint8Array>({
        start(controller) { readableController = controller; },
      }),
      writable: new WritableStream<Uint8Array>({
        write(value) {
          writes.push(Uint8Array.from(value));
          const text = new TextDecoder().decode(value);
          if (text === "\r") readableController.enqueue(new TextEncoder().encode("\r\n>: "));
          if (text === "start_rpc_session\r") {
            readableController.enqueue(new TextEncoder().encode("start_rpc_session\r\n"));
          }
        },
      }),
      async open(options: { baudRate: number }) { expect(options.baudRate).toBe(230400); },
      async close() { readableController.close(); },
      getInfo() { return { usbVendorId: 0x0483, usbProductId: 0x5740 }; },
    });
    Object.defineProperty(globalThis, "navigator", {
      configurable: true,
      value: {
        bluetooth: (navigator as Navigator & { bluetooth?: unknown }).bluetooth,
        serial: {
          async getPorts() { return [port]; },
          async requestPort() { return port; },
        },
      },
    });
    const transport = new WebSerialTransport();
    const [device] = await transport.discover();

    await transport.connect(device!);

    expect(writes.map((value) => new TextDecoder().decode(value))).toEqual([
      "\r",
      "start_rpc_session\r",
    ]);
    expect(transport.health).toMatchObject({ connected: true, writable: true });
    await transport.close();
  });
});

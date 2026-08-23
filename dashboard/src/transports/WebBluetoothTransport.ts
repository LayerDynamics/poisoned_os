import { DiscoveredDevice, Transport, TransportError, TransportHealth, throwIfAborted } from "./Transport";

interface BluetoothCharacteristicLike extends EventTarget {
  readonly value?: DataView | null;
  readValue(): Promise<DataView>;
  startNotifications(): Promise<BluetoothCharacteristicLike>;
  stopNotifications(): Promise<BluetoothCharacteristicLike>;
  writeValue?(value: BufferSource): Promise<void>;
  writeValueWithResponse?(value: BufferSource): Promise<void>;
  writeValueWithoutResponse?(value: BufferSource): Promise<void>;
}

interface BluetoothServiceLike {
  getCharacteristic(uuid: string): Promise<BluetoothCharacteristicLike>;
}

interface BluetoothServerLike {
  readonly connected: boolean;
  connect(): Promise<BluetoothServerLike>;
  disconnect(): void;
  getPrimaryService(uuid: string): Promise<BluetoothServiceLike>;
}

interface BluetoothDeviceLike extends EventTarget {
  readonly id: string;
  readonly name?: string | null;
  readonly gatt?: BluetoothServerLike;
}

interface BluetoothApiLike {
  requestDevice(options: {
    filters: Array<{ services: string[] }>;
  }): Promise<BluetoothDeviceLike>;
}

interface PendingRead {
  resolve(value: Uint8Array | null): void;
  reject(error: TransportError): void;
  signal?: AbortSignal;
  abort?: () => void;
}

// The firmware UUID byte arrays in serial_service_uuid.inc are stored in
// little-endian controller order. These are their canonical Web Bluetooth UUIDs.
export const FLIPPER_BLE_SERIAL_UUIDS = {
  service: "8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000",
  tx: "19ed82ae-ed21-4c9d-4145-228e61fe0000",
  rx: "19ed82ae-ed21-4c9d-4145-228e62fe0000",
  flowControl: "19ed82ae-ed21-4c9d-4145-228e63fe0000",
  rpcStatus: "19ed82ae-ed21-4c9d-4145-228e64fe0000",
} as const;

const BLE_CHARACTERISTIC_CHUNK = 243;

function bluetoothApi(): BluetoothApiLike {
  const bluetooth = (navigator as Navigator & { bluetooth?: BluetoothApiLike }).bluetooth;
  if (!bluetooth) throw new TransportError("unsupported", "Web Bluetooth is unavailable");
  return bluetooth;
}

function copyValue(value: DataView | null | undefined): Uint8Array | null {
  if (!value || value.byteLength === 0) return null;
  return new Uint8Array(value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength));
}

function decodeFlowCredit(value: DataView): number {
  if (value.byteLength !== 4) {
    throw new TransportError("io", "Flipper BLE flow-control value must be four bytes");
  }
  return value.getUint32(0, false);
}

export class WebBluetoothTransport implements Transport {
  public readonly kind = "bluetooth" as const;
  public readonly mtu = 1024;
  private readonly devices = new Map<string, BluetoothDeviceLike>();
  private device: BluetoothDeviceLike | null = null;
  private server: BluetoothServerLike | null = null;
  private tx: BluetoothCharacteristicLike | null = null;
  private rx: BluetoothCharacteristicLike | null = null;
  private flowControl: BluetoothCharacteristicLike | null = null;
  private rpcStatus: BluetoothCharacteristicLike | null = null;
  private credits = 0;
  private queuedBytes = 0;
  private lastError: string | undefined;
  private readonly frames: Uint8Array[] = [];
  private readonly readers: PendingRead[] = [];
  private readonly creditWaiters: Array<() => void> = [];
  private readonly disconnectHandlers = new Set<() => void>();
  private writeQueue: Promise<void> = Promise.resolve();

  private readonly onTxValue = (event: Event): void => {
    const characteristic = event.target as BluetoothCharacteristicLike | null;
    const frame = copyValue(characteristic?.value);
    if (!frame) return;
    const reader = this.readers.shift();
    if (reader) {
      if (reader.abort) reader.signal?.removeEventListener("abort", reader.abort);
      reader.resolve(frame);
    } else {
      this.frames.push(frame);
    }
  };

  private readonly onFlowControlValue = (event: Event): void => {
    const characteristic = event.target as BluetoothCharacteristicLike | null;
    try {
      if (characteristic?.value) this.setCredits(decodeFlowCredit(characteristic.value));
    } catch (error) {
      this.lastError = error instanceof Error ? error.message : String(error);
    }
  };

  private readonly onDisconnected = (): void => {
    this.resetConnection();
    for (const handler of this.disconnectHandlers) handler();
  };

  public get health(): TransportHealth {
    const connected = this.server?.connected === true && this.rx !== null;
    return { connected, writable: connected && this.credits > 0, queuedBytes: this.queuedBytes, lastError: this.lastError };
  }

  public async discover(signal?: AbortSignal): Promise<readonly DiscoveredDevice[]> {
    throwIfAborted(signal);
    const device = await bluetoothApi().requestDevice({
      filters: [{ services: [FLIPPER_BLE_SERIAL_UUIDS.service] }],
    });
    throwIfAborted(signal);
    this.devices.set(device.id, device);
    return [{
      id: device.id,
      label: device.name ?? "Flipper Zero",
      kind: "bluetooth",
      metadata: { serviceUuid: FLIPPER_BLE_SERIAL_UUIDS.service },
    }];
  }

  public async connect(device: DiscoveredDevice, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    if (device.kind !== "bluetooth") throw new TransportError("io", "device is not a Bluetooth device");
    bluetoothApi();
    const selected = this.devices.get(device.id);
    if (!selected) {
      throw new TransportError("io", "Bluetooth device must be selected through discovery before connecting");
    }
    if (!selected.gatt) throw new TransportError("io", "selected Bluetooth device has no GATT server");

    try {
      const server = await selected.gatt.connect();
      throwIfAborted(signal);
      const service = await server.getPrimaryService(FLIPPER_BLE_SERIAL_UUIDS.service);
      const [tx, rx, flowControl, rpcStatus] = await Promise.all([
        service.getCharacteristic(FLIPPER_BLE_SERIAL_UUIDS.tx),
        service.getCharacteristic(FLIPPER_BLE_SERIAL_UUIDS.rx),
        service.getCharacteristic(FLIPPER_BLE_SERIAL_UUIDS.flowControl),
        service.getCharacteristic(FLIPPER_BLE_SERIAL_UUIDS.rpcStatus),
      ]);
      tx.addEventListener("characteristicvaluechanged", this.onTxValue);
      flowControl.addEventListener("characteristicvaluechanged", this.onFlowControlValue);
      await tx.startNotifications();
      await flowControl.startNotifications();
      const flowValue = await flowControl.readValue();
      const credits = decodeFlowCredit(flowValue);
      const writeStatus = rpcStatus.writeValueWithResponse ?? rpcStatus.writeValue;
      if (!writeStatus) throw new TransportError("io", "Flipper BLE RPC-status characteristic is not writable");
      await writeStatus.call(rpcStatus, new Uint8Array([1, 0, 0, 0]));
      throwIfAborted(signal);

      this.device = selected;
      this.server = server;
      this.tx = tx;
      this.rx = rx;
      this.flowControl = flowControl;
      this.rpcStatus = rpcStatus;
      this.setCredits(credits);
      selected.addEventListener("gattserverdisconnected", this.onDisconnected);
    } catch (error) {
      selected.gatt.disconnect();
      this.resetConnection();
      if (error instanceof TransportError) throw error;
      this.lastError = error instanceof Error ? error.message : String(error);
      throw new TransportError("io", this.lastError);
    }
  }

  public async read(signal?: AbortSignal): Promise<Uint8Array | null> {
    throwIfAborted(signal);
    const frame = this.frames.shift();
    if (frame) return frame;
    if (!this.health.connected) throw new TransportError("not-connected", "Bluetooth transport is not connected");
    return new Promise<Uint8Array | null>((resolve, reject) => {
      const pending: PendingRead = { resolve, reject, signal };
      pending.abort = () => {
        const index = this.readers.indexOf(pending);
        if (index >= 0) this.readers.splice(index, 1);
        reject(new TransportError("aborted", "Bluetooth read aborted"));
      };
      signal?.addEventListener("abort", pending.abort, { once: true });
      this.readers.push(pending);
    });
  }

  public async write(frame: Uint8Array, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    if (frame.byteLength > this.mtu) throw new TransportError("frame-too-large", "frame exceeds transport MTU");
    if (!this.health.connected) throw new TransportError("not-connected", "Bluetooth transport is not connected");
    this.queuedBytes += frame.byteLength;
    const operation = this.writeQueue.then(() => this.writeFrame(frame, signal));
    this.writeQueue = operation.catch(() => undefined);
    try {
      await operation;
    } finally {
      this.queuedBytes -= frame.byteLength;
    }
  }

  public async close(): Promise<void> {
    const device = this.device;
    const tx = this.tx;
    const flowControl = this.flowControl;
    const rpcStatus = this.rpcStatus;
    try {
      const writeStatus = rpcStatus?.writeValueWithResponse ?? rpcStatus?.writeValue;
      if (rpcStatus && writeStatus) {
        await writeStatus.call(rpcStatus, new Uint8Array([0, 0, 0, 0]));
      }
    } catch (error) {
      this.lastError = error instanceof Error ? error.message : String(error);
    }
    tx?.removeEventListener("characteristicvaluechanged", this.onTxValue);
    flowControl?.removeEventListener("characteristicvaluechanged", this.onFlowControlValue);
    await Promise.allSettled([tx?.stopNotifications(), flowControl?.stopNotifications()]);
    device?.removeEventListener("gattserverdisconnected", this.onDisconnected);
    this.server?.disconnect();
    this.resetConnection();
  }

  public onDisconnect(handler: () => void): () => void {
    this.disconnectHandlers.add(handler);
    return () => this.disconnectHandlers.delete(handler);
  }

  private async writeFrame(frame: Uint8Array, signal?: AbortSignal): Promise<void> {
    let offset = 0;
    while (offset < frame.byteLength) {
      throwIfAborted(signal);
      await this.waitForCredits(signal);
      const rx = this.rx;
      if (!rx || !this.health.connected) {
        throw new TransportError("not-connected", "Bluetooth transport disconnected while writing");
      }
      const length = Math.min(BLE_CHARACTERISTIC_CHUNK, this.credits, frame.byteLength - offset);
      const chunk = frame.slice(offset, offset + length);
      const write = rx.writeValueWithoutResponse ?? rx.writeValue;
      if (!write) throw new TransportError("io", "Flipper BLE RX characteristic is not writable");
      try {
        await write.call(rx, chunk);
      } catch (error) {
        this.lastError = error instanceof Error ? error.message : String(error);
        throw new TransportError("io", this.lastError);
      }
      this.credits -= length;
      offset += length;
    }
  }

  private async waitForCredits(signal?: AbortSignal): Promise<void> {
    if (this.credits > 0) return;
    if (!this.health.connected) throw new TransportError("not-connected", "Bluetooth transport is not connected");
    await new Promise<void>((resolve, reject) => {
      const wake = () => {
        signal?.removeEventListener("abort", abort);
        resolve();
      };
      const abort = () => {
        const index = this.creditWaiters.indexOf(wake);
        if (index >= 0) this.creditWaiters.splice(index, 1);
        reject(new TransportError("aborted", "Bluetooth write aborted"));
      };
      signal?.addEventListener("abort", abort, { once: true });
      this.creditWaiters.push(wake);
    });
    throwIfAborted(signal);
  }

  private setCredits(credits: number): void {
    this.credits = credits;
    if (credits > 0) {
      for (const wake of this.creditWaiters.splice(0)) wake();
    }
  }

  private resetConnection(): void {
    this.device = null;
    this.server = null;
    this.tx = null;
    this.rx = null;
    this.flowControl = null;
    this.rpcStatus = null;
    this.credits = 0;
    this.frames.length = 0;
    for (const pending of this.readers.splice(0)) {
      if (pending.abort) pending.signal?.removeEventListener("abort", pending.abort);
      pending.resolve(null);
    }
    for (const wake of this.creditWaiters.splice(0)) wake();
  }
}

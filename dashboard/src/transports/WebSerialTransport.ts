import {
  DiscoveredDevice,
  Transport,
  TransportError,
  TransportHealth,
  throwIfAborted,
} from "./Transport";

interface SerialPortLike extends EventTarget {
  readable: ReadableStream<Uint8Array> | null;
  writable: WritableStream<Uint8Array> | null;
  open(options: { baudRate: number }): Promise<void>;
  close(): Promise<void>;
  getInfo?(): { usbVendorId?: number; usbProductId?: number };
}

interface SerialApiLike {
  getPorts(): Promise<SerialPortLike[]>;
  requestPort(options?: { filters: Array<{ usbVendorId: number; usbProductId: number }> }): Promise<SerialPortLike>;
}

const FLIPPER_USB_VENDOR_ID = 0x0483;
const FLIPPER_RUNTIME_PRODUCT_ID = 0x5740;
const FLIPPER_CLI_TIMEOUT_MS = 10_000;
const ascii = new TextEncoder();

function isFlipperRuntime(port: SerialPortLike): boolean {
  const info = port.getInfo?.();
  return info?.usbVendorId === FLIPPER_USB_VENDOR_ID &&
    info.usbProductId === FLIPPER_RUNTIME_PRODUCT_ID;
}

export class WebSerialTransport implements Transport {
  public readonly kind = "serial" as const;
  public readonly mtu = 1024;
  private port: SerialPortLike | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private queuedBytes = 0;
  private lastError: string | undefined;
  private readonly disconnectHandlers = new Set<() => void>();
  private readonly onPortDisconnect = (): void => {
    for (const handler of this.disconnectHandlers) handler();
  };

  public get health(): TransportHealth {
    return { connected: this.port !== null, writable: this.writer !== null, queuedBytes: this.queuedBytes, lastError: this.lastError };
  }

  public async discover(signal?: AbortSignal): Promise<readonly DiscoveredDevice[]> {
    throwIfAborted(signal);
    const api = (navigator as Navigator & { serial?: SerialApiLike }).serial;
    if (!api) throw new TransportError("unsupported", "Web Serial is unavailable");
    const ports = (await api.getPorts()).filter(isFlipperRuntime);
    return ports.map((_, index) => ({
      id: `serial-${index}`,
      label: `Flipper Zero ${index + 1}`,
      kind: "serial",
      metadata: { usbVendorId: "0483", usbProductId: "5740" },
    }));
  }

  public async connect(device: DiscoveredDevice, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    if (device.kind !== "serial") throw new TransportError("io", "device is not a serial device");
    const api = (navigator as Navigator & { serial?: SerialApiLike }).serial;
    if (!api) throw new TransportError("unsupported", "Web Serial is unavailable");
    const ports = (await api.getPorts()).filter(isFlipperRuntime);
    const index = device.id === "serial-request" ? -1 : Number(device.id.replace("serial-", ""));
    const port = ports[index] ?? await api.requestPort({
      filters: [{
        usbVendorId: FLIPPER_USB_VENDOR_ID,
        usbProductId: FLIPPER_RUNTIME_PRODUCT_ID,
      }],
    });
    if (!isFlipperRuntime(port)) {
      throw new TransportError("io", "selected device is not a Flipper Zero runtime (0483:5740)");
    }
    await port.open({ baudRate: 230400 });
    const reader = port.readable?.getReader() ?? null;
    const writer = port.writable?.getWriter() ?? null;
    if (!reader || !writer) {
      await port.close();
      throw new TransportError("io", "Flipper serial streams are unavailable");
    }
    try {
      await this.enterRpcMode(reader, writer, signal);
    } catch (error) {
      try { await reader.cancel(); } catch { /* Preserve the handshake error. */ }
      try { reader.releaseLock(); } catch { /* Preserve the handshake error. */ }
      try { writer.releaseLock(); } catch { /* Preserve the handshake error. */ }
      try { await port.close(); } catch { /* Preserve the handshake error. */ }
      if (error instanceof TransportError) throw error;
      throw new TransportError("io", error instanceof Error ? error.message : String(error));
    }
    this.port = port;
    port.addEventListener("disconnect", this.onPortDisconnect);
    this.reader = reader;
    this.writer = writer;
  }

  public async read(signal?: AbortSignal): Promise<Uint8Array | null> {
    throwIfAborted(signal);
    if (!this.reader) throw new TransportError("not-connected", "serial transport is not connected");
    const result = await this.reader.read();
    return result.done ? null : result.value;
  }

  public async write(frame: Uint8Array, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    if (frame.byteLength > this.mtu) throw new TransportError("frame-too-large", "frame exceeds transport MTU");
    if (!this.writer) throw new TransportError("not-connected", "serial transport is not connected");
    this.queuedBytes += frame.byteLength;
    try {
      await this.writer.write(frame);
    } catch (error) {
      this.lastError = error instanceof Error ? error.message : String(error);
      throw new TransportError("io", this.lastError);
    } finally {
      this.queuedBytes -= frame.byteLength;
    }
  }

  public async close(): Promise<void> {
    const port = this.port;
    const reader = this.reader;
    const writer = this.writer;
    port?.removeEventListener("disconnect", this.onPortDisconnect);
    this.reader = null;
    this.writer = null;
    this.port = null;
    let cleanupError: unknown;
    try { await reader?.cancel(); } catch (error) { cleanupError = error; }
    try { reader?.releaseLock(); } catch (error) { cleanupError ??= error; }
    try { writer?.releaseLock(); } catch (error) { cleanupError ??= error; }
    try { await port?.close(); } catch (error) { cleanupError ??= error; }
    if (cleanupError) this.lastError = cleanupError instanceof Error ? cleanupError.message : String(cleanupError);
  }

  public onDisconnect(handler: () => void): () => void {
    this.disconnectHandlers.add(handler);
    return () => this.disconnectHandlers.delete(handler);
  }

  private async enterRpcMode(
    reader: ReadableStreamDefaultReader<Uint8Array>,
    writer: WritableStreamDefaultWriter<Uint8Array>,
    signal?: AbortSignal,
  ): Promise<void> {
    await writer.write(ascii.encode("\r"));
    await this.readUntil(reader, ascii.encode(">: "), signal);
    await writer.write(ascii.encode("start_rpc_session\r"));
    await this.readUntil(reader, ascii.encode("\n"), signal);
  }

  private async readUntil(
    reader: ReadableStreamDefaultReader<Uint8Array>,
    marker: Uint8Array,
    signal?: AbortSignal,
  ): Promise<void> {
    const received: number[] = [];
    const deadline = Date.now() + FLIPPER_CLI_TIMEOUT_MS;
    while (Date.now() < deadline && received.length <= 4096) {
      throwIfAborted(signal);
      const remaining = deadline - Date.now();
      const result = await new Promise<ReadableStreamReadResult<Uint8Array>>((resolve, reject) => {
        const timeout = setTimeout(
          () => reject(new TransportError("io", "Flipper CLI did not enter RPC mode")),
          remaining,
        );
        const aborted = () => {
          clearTimeout(timeout);
          reject(new TransportError("aborted", "serial RPC startup aborted"));
        };
        signal?.addEventListener("abort", aborted, { once: true });
        reader.read().then(resolve, reject).finally(() => {
          clearTimeout(timeout);
          signal?.removeEventListener("abort", aborted);
        });
      });
      if (result.done) throw new TransportError("io", "Flipper CLI closed before RPC mode started");
      received.push(...result.value);
      for (let offset = 0; offset <= received.length - marker.byteLength; offset += 1) {
        if (marker.every((value, markerIndex) => received[offset + markerIndex] === value)) return;
      }
    }
    throw new TransportError("io", "Flipper CLI did not enter RPC mode");
  }
}

export const FLIPPER_USB_VENDOR_ID = 0x0483;
export const FLIPPER_RUNTIME_PRODUCT_ID = 0x5740;
export const SERIAL_BAUD_RATE = 230_400;
const HANDSHAKE_TIMEOUT_MS = 10_000;
const ascii = new TextEncoder();

export interface ByteTransport {
  read(signal?: AbortSignal): Promise<Uint8Array | null>;
  write(data: Uint8Array, signal?: AbortSignal): Promise<void>;
  close(): Promise<void>;
}

export interface SerialPortLike extends EventTarget {
  readonly readable: ReadableStream<Uint8Array> | null;
  readonly writable: WritableStream<Uint8Array> | null;
  open(options: { baudRate: number; bufferSize?: number }): Promise<void>;
  close(): Promise<void>;
  getInfo(): { usbVendorId?: number; usbProductId?: number };
}

export interface SerialApiLike extends EventTarget {
  getPorts(): Promise<SerialPortLike[]>;
  requestPort(options: { filters: Array<{ usbVendorId: number; usbProductId: number }> }): Promise<SerialPortLike>;
}

export class SerialConnectionError extends Error {
  public constructor(message: string) {
    super(message);
    this.name = "SerialConnectionError";
  }
}

export function serialApi(): SerialApiLike | null {
  return (navigator as Navigator & { serial?: SerialApiLike }).serial ?? null;
}

export function isFlipperRuntime(port: SerialPortLike): boolean {
  const info = port.getInfo();
  return info.usbVendorId === FLIPPER_USB_VENDOR_ID && info.usbProductId === FLIPPER_RUNTIME_PRODUCT_ID;
}

function abortError(): SerialConnectionError {
  return new SerialConnectionError("The serial operation was cancelled");
}

function assertNotAborted(signal?: AbortSignal): void {
  if (signal?.aborted) throw abortError();
}

export class WebSerialConnection implements ByteTransport {
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private open = false;

  public constructor(public readonly port: SerialPortLike) {
    if (!isFlipperRuntime(port)) {
      throw new SerialConnectionError("The selected USB device is not a Flipper Zero runtime (0483:5740)");
    }
  }

  public static async request(signal?: AbortSignal): Promise<WebSerialConnection> {
    assertNotAborted(signal);
    if (!window.isSecureContext) throw new SerialConnectionError("Open this installer over HTTPS or localhost");
    const api = serialApi();
    if (!api) throw new SerialConnectionError("Web Serial is unavailable; use current Chrome, Edge, or another Chromium browser");
    const port = await api.requestPort({
      filters: [{ usbVendorId: FLIPPER_USB_VENDOR_ID, usbProductId: FLIPPER_RUNTIME_PRODUCT_ID }],
    });
    assertNotAborted(signal);
    return new WebSerialConnection(port);
  }

  public async connect(signal?: AbortSignal): Promise<void> {
    assertNotAborted(signal);
    if (this.open) return;
    try {
      await this.port.open({ baudRate: SERIAL_BAUD_RATE, bufferSize: 16_384 });
      const reader = this.port.readable?.getReader() ?? null;
      const writer = this.port.writable?.getWriter() ?? null;
      if (!reader || !writer) throw new SerialConnectionError("The Flipper USB serial streams are unavailable");
      this.reader = reader;
      this.writer = writer;
      await this.enterRpcMode(signal);
      this.open = true;
    } catch (error) {
      await this.release().catch(() => undefined);
      if (error instanceof SerialConnectionError) throw error;
      throw new SerialConnectionError(error instanceof Error ? error.message : String(error));
    }
  }

  public async read(signal?: AbortSignal): Promise<Uint8Array | null> {
    assertNotAborted(signal);
    const reader = this.reader;
    if (!reader) throw new SerialConnectionError("The Flipper serial connection is not open");
    let rejectAbort: ((reason: SerialConnectionError) => void) | null = null;
    const abortPromise = new Promise<never>((_resolve, reject) => {
      rejectAbort = reject;
    });
    const onAbort = () => {
      void reader.cancel().catch(() => undefined);
      rejectAbort?.(abortError());
    };
    signal?.addEventListener("abort", onAbort, { once: true });
    if (signal?.aborted) onAbort();
    try {
      const result = await Promise.race([reader.read(), abortPromise]);
      return result.done ? null : result.value;
    } finally {
      signal?.removeEventListener("abort", onAbort);
    }
  }

  public async write(data: Uint8Array, signal?: AbortSignal): Promise<void> {
    assertNotAborted(signal);
    const writer = this.writer;
    if (!writer) throw new SerialConnectionError("The Flipper serial connection is not open");
    await writer.write(data);
  }

  public async close(): Promise<void> {
    await this.release();
  }

  private async release(): Promise<void> {
    const reader = this.reader;
    const writer = this.writer;
    this.reader = null;
    this.writer = null;
    this.open = false;
    let failure: unknown;
    try { await reader?.cancel(); } catch (error) { failure = error; }
    try { reader?.releaseLock(); } catch (error) { failure ??= error; }
    try { writer?.releaseLock(); } catch (error) { failure ??= error; }
    try { await this.port.close(); } catch (error) { failure ??= error; }
    if (failure) throw new SerialConnectionError(failure instanceof Error ? failure.message : String(failure));
  }

  private async enterRpcMode(signal?: AbortSignal): Promise<void> {
    const reader = this.reader;
    const writer = this.writer;
    if (!reader || !writer) throw new SerialConnectionError("The Flipper serial connection is not open");
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
    const deadline = Date.now() + HANDSHAKE_TIMEOUT_MS;
    const received: number[] = [];
    while (Date.now() < deadline && received.length <= 4096) {
      assertNotAborted(signal);
      const remaining = deadline - Date.now();
      const result = await new Promise<ReadableStreamReadResult<Uint8Array>>((resolve, reject) => {
        const timeout = globalThis.setTimeout(
          () => reject(new SerialConnectionError("The Flipper CLI did not enter RPC mode; unlock the device and retry")),
          remaining,
        );
        const aborted = () => reject(abortError());
        signal?.addEventListener("abort", aborted, { once: true });
        reader.read().then(resolve, reject).finally(() => {
          globalThis.clearTimeout(timeout);
          signal?.removeEventListener("abort", aborted);
        });
      });
      if (result.done) throw new SerialConnectionError("The Flipper disconnected before RPC mode started");
      received.push(...result.value);
      for (let offset = 0; offset <= received.length - marker.byteLength; offset += 1) {
        if (marker.every((value, index) => received[offset + index] === value)) return;
      }
    }
    throw new SerialConnectionError("The Flipper CLI did not enter RPC mode; unlock the device and retry");
  }
}

export async function waitForAuthorizedFlipper(
  preferred: SerialPortLike,
  timeoutMs: number,
  onWait?: (elapsedMs: number) => void,
  signal?: AbortSignal,
): Promise<WebSerialConnection> {
  const api = serialApi();
  if (!api) throw new SerialConnectionError("Web Serial became unavailable");
  const started = Date.now();
  let lastError = "Flipper USB runtime has not returned";
  while (Date.now() - started < timeoutMs) {
    assertNotAborted(signal);
    onWait?.(Date.now() - started);
    const ports = (await api.getPorts()).filter(isFlipperRuntime);
    const candidates = ports.includes(preferred) ? [preferred] : ports;
    if (candidates.length > 1 && !candidates.includes(preferred)) {
      throw new SerialConnectionError("More than one authorized Flipper is connected; disconnect all but the device being installed");
    }
    const candidate = candidates[0];
    if (candidate) {
      const connection = new WebSerialConnection(candidate);
      try {
        await connection.connect(signal);
        return connection;
      } catch (error) {
        lastError = error instanceof Error ? error.message : String(error);
      }
    }
    await new Promise<void>((resolve, reject) => {
      const onAbort = () => {
        globalThis.clearTimeout(timeout);
        signal?.removeEventListener("abort", onAbort);
        reject(abortError());
      };
      const timeout = globalThis.setTimeout(() => {
        signal?.removeEventListener("abort", onAbort);
        resolve();
      }, 1_000);
      signal?.addEventListener("abort", onAbort, { once: true });
      if (signal?.aborted) onAbort();
    });
  }
  throw new SerialConnectionError(`Poisoned_Os did not return over USB: ${lastError}`);
}

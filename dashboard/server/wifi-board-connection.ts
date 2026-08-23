import { createConnection, type Socket } from "node:net";
import {
  EXPANSION_BAUD_CHANGE_DELAY_MS,
  EXPANSION_MAX_DATA_SIZE,
  EXPANSION_TIMEOUT_MS,
  ExpansionControlCommand,
  ExpansionDecoder,
  ExpansionFrameType,
  encodeBaudRate,
  encodeControl,
  encodeData,
  encodeHeartbeat,
  encodeStatus,
  type ExpansionFrame,
} from "./expansion";
import type { RuntimeBoardConfig } from "./runtime-config";

export interface ByteSocket {
  write(data: Uint8Array): Promise<void>;
  onData(listener: (data: Uint8Array) => void): () => void;
  onClose(listener: (error?: Error) => void): () => void;
  close(): Promise<void>;
}

export interface UartConfig {
  bitRate: number;
  stopBits: number;
  parity: number;
  dataBits: number;
}

export interface BoardConnectionIo {
  setUartConfig(boardUrl: URL, config: UartConfig): Promise<void>;
  connectTcp(host: string, port: number): Promise<ByteSocket>;
  delay(milliseconds: number): Promise<void>;
  scheduleRepeating(callback: () => void, intervalMs: number): () => void;
}

const INITIAL_UART_CONFIG: UartConfig = {
  bitRate: 9_600,
  stopBits: 0,
  parity: 0,
  dataBits: 8,
};
const SESSION_UART_CONFIG: UartConfig = {
  bitRate: 230_400,
  stopBits: 0,
  parity: 0,
  dataBits: 8,
};
const HEARTBEAT_INTERVAL_MS = 100;

class NodeByteSocket implements ByteSocket {
  constructor(private readonly socket: Socket) {}

  write(data: Uint8Array): Promise<void> {
    return new Promise((resolve, reject) => {
      this.socket.write(data, (error) => {
        if (error) reject(error);
        else resolve();
      });
    });
  }

  onData(listener: (data: Uint8Array) => void): () => void {
    const handler = (data: Buffer) => listener(new Uint8Array(data));
    this.socket.on("data", handler);
    return () => this.socket.off("data", handler);
  }

  onClose(listener: (error?: Error) => void): () => void {
    let lastError: Error | undefined;
    const errorHandler = (error: Error) => {
      lastError = error;
    };
    const closeHandler = () => listener(lastError);
    this.socket.on("error", errorHandler);
    this.socket.on("close", closeHandler);
    return () => {
      this.socket.off("error", errorHandler);
      this.socket.off("close", closeHandler);
    };
  }

  close(): Promise<void> {
    if (this.socket.destroyed) return Promise.resolve();
    return new Promise((resolve) => {
      this.socket.end(resolve);
    });
  }
}

export class NodeBoardConnectionIo implements BoardConnectionIo {
  async setUartConfig(boardUrl: URL, config: UartConfig): Promise<void> {
    const endpoint = new URL("/api/v1/uart/set_config", boardUrl);
    const response = await fetch(endpoint, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        bit_rate: config.bitRate,
        stop_bits: config.stopBits,
        parity: config.parity,
        data_bits: config.dataBits,
      }),
      signal: AbortSignal.timeout(2_000),
    });
    if (!response.ok) {
      throw new Error(`Wi-Fi board UART configuration failed with HTTP ${response.status}`);
    }

    const body = await response.json() as { result?: string; error?: string };
    if (body.result !== "OK") {
      throw new Error(`Wi-Fi board rejected UART configuration: ${body.error ?? "unknown response"}`);
    }
  }

  connectTcp(host: string, port: number): Promise<ByteSocket> {
    return new Promise((resolve, reject) => {
      const socket = createConnection({ host, port });
      const onError = (error: Error) => {
        socket.off("connect", onConnect);
        socket.destroy();
        reject(error);
      };
      const onConnect = () => {
        socket.off("error", onError);
        socket.setKeepAlive(true, 5_000);
        socket.setNoDelay(true);
        resolve(new NodeByteSocket(socket));
      };
      socket.once("error", onError);
      socket.once("connect", onConnect);
    });
  }

  delay(milliseconds: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, milliseconds));
  }

  scheduleRepeating(callback: () => void, intervalMs: number): () => void {
    const timer = setInterval(callback, intervalMs);
    timer.unref();
    return () => clearInterval(timer);
  }
}

interface FrameWaiter {
  type: ExpansionFrameType;
  resolve: (frame: ExpansionFrame) => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout>;
}

export type WifiBoardConnectionState =
  | "disconnected"
  | "connecting"
  | "ready"
  | "closing"
  | "closed"
  | "error";

export class WifiBoardConnection {
  state: WifiBoardConnectionState = "disconnected";

  private socket?: ByteSocket;
  private readonly decoder = new ExpansionDecoder();
  private readonly waiters: FrameWaiter[] = [];
  private readonly rpcListeners = new Set<(data: Uint8Array) => void>();
  private readonly disconnectListeners = new Set<(error: Error) => void>();
  private removeDataListener?: () => void;
  private removeCloseListener?: () => void;
  private cancelHeartbeat?: () => void;
  private disconnectError?: Error;
  private writeTail: Promise<void> = Promise.resolve();
  private operationTail: Promise<void> = Promise.resolve();

  constructor(
    readonly board: RuntimeBoardConfig,
    private readonly io: BoardConnectionIo = new NodeBoardConnectionIo(),
  ) {}

  async connect(): Promise<void> {
    if (this.state !== "disconnected") {
      throw new Error(`Wi-Fi board ${this.board.id} cannot connect from state ${this.state}`);
    }
    this.state = "connecting";

    try {
      await this.io.setUartConfig(this.board.httpUrl, INITIAL_UART_CONFIG);
      this.socket = await this.io.connectTcp(this.board.httpUrl.hostname, this.board.tcpPort);
      this.removeDataListener = this.socket.onData((data) => this.receive(data));
      this.removeCloseListener = this.socket.onClose((error) => this.socketClosed(error));

      const heartbeat = this.waitForFrame(ExpansionFrameType.Heartbeat);
      await this.write(Uint8Array.of(0x00));
      await heartbeat;

      this.requireOkStatus(await this.exchange(
        encodeBaudRate(SESSION_UART_CONFIG.bitRate),
        ExpansionFrameType.Status,
      ));
      await this.io.setUartConfig(this.board.httpUrl, SESSION_UART_CONFIG);
      await this.io.delay(EXPANSION_BAUD_CHANGE_DELAY_MS);

      this.requireOkStatus(await this.exchange(
        encodeControl(ExpansionControlCommand.StartRpc),
        ExpansionFrameType.Status,
      ));

      this.state = "ready";
      this.cancelHeartbeat = this.io.scheduleRepeating(() => {
        void this.enqueueOperation(async () => {
          if (this.state !== "ready") return;
          await this.exchange(encodeHeartbeat(), ExpansionFrameType.Heartbeat);
        }).catch((error: unknown) => this.fail(this.asError(error)));
      }, HEARTBEAT_INTERVAL_MS);
    } catch (error) {
      const connectionError = this.asError(error);
      this.fail(connectionError);
      if (this.socket) await this.socket.close().catch(() => undefined);
      throw connectionError;
    }
  }

  sendRpc(data: Uint8Array): Promise<void> {
    if (this.state !== "ready") {
      return Promise.reject(new Error(`Wi-Fi board ${this.board.id} RPC session is not ready`));
    }

    return this.enqueueOperation(async () => {
      for (let offset = 0; offset < data.byteLength; offset += EXPANSION_MAX_DATA_SIZE) {
        const chunk = data.subarray(offset, offset + EXPANSION_MAX_DATA_SIZE);
        this.requireOkStatus(await this.exchange(encodeData(chunk), ExpansionFrameType.Status));
      }
    });
  }

  onRpcData(listener: (data: Uint8Array) => void): () => void {
    this.rpcListeners.add(listener);
    return () => this.rpcListeners.delete(listener);
  }

  onDisconnect(listener: (error: Error) => void): () => void {
    this.disconnectListeners.add(listener);
    if (this.disconnectError) {
      const error = this.disconnectError;
      queueMicrotask(() => {
        if (this.disconnectListeners.has(listener)) listener(error);
      });
    }
    return () => this.disconnectListeners.delete(listener);
  }

  async close(): Promise<void> {
    if (this.state === "closed" || this.state === "disconnected") {
      this.state = "closed";
      return;
    }

    const wasReady = this.state === "ready";
    this.state = "closing";
    this.cancelHeartbeat?.();
    this.cancelHeartbeat = undefined;

    try {
      if (wasReady) {
        await this.enqueueOperation(async () => {
          this.requireOkStatus(await this.exchange(
            encodeControl(ExpansionControlCommand.StopRpc),
            ExpansionFrameType.Status,
          ));
        });
      }
    } finally {
      await this.socket?.close().catch(() => undefined);
      this.detachSocket();
      this.rejectWaiters(new Error(`Wi-Fi board ${this.board.id} connection closed`));
      this.state = "closed";
    }
  }

  private enqueueOperation<T>(operation: () => Promise<T>): Promise<T> {
    const result = this.operationTail.then(operation);
    this.operationTail = result.then(() => undefined, () => undefined);
    return result;
  }

  private exchange(data: Uint8Array, responseType: ExpansionFrameType): Promise<ExpansionFrame> {
    const response = this.waitForFrame(responseType);
    return this.write(data).then(() => response, (error) => {
      this.rejectWaiter(responseType, this.asError(error));
      throw error;
    });
  }

  private write(data: Uint8Array): Promise<void> {
    if (!this.socket) return Promise.reject(new Error("Wi-Fi board TCP socket is not connected"));
    this.writeTail = this.writeTail.then(() => this.socket?.write(data));
    return this.writeTail;
  }

  private waitForFrame(type: ExpansionFrameType): Promise<ExpansionFrame> {
    return new Promise((resolve, reject) => {
      const waiter: FrameWaiter = {
        type,
        resolve,
        reject,
        timer: setTimeout(() => {
          const index = this.waiters.indexOf(waiter);
          if (index >= 0) this.waiters.splice(index, 1);
          reject(new Error(`Wi-Fi board ${this.board.id} timed out waiting for expansion frame ${type}`));
        }, EXPANSION_TIMEOUT_MS),
      };
      this.waiters.push(waiter);
    });
  }

  private receive(data: Uint8Array): void {
    let frames: ExpansionFrame[];
    try {
      frames = this.decoder.push(data);
    } catch (error) {
      this.fail(this.asError(error));
      return;
    }

    for (const frame of frames) {
      const waiterIndex = this.waiters.findIndex((waiter) => waiter.type === frame.type);
      if (waiterIndex >= 0) {
        const [waiter] = this.waiters.splice(waiterIndex, 1);
        clearTimeout(waiter.timer);
        waiter.resolve(frame);
        continue;
      }

      if (frame.type === ExpansionFrameType.Data && this.state === "ready") {
        void this.write(encodeStatus(0)).catch((error: unknown) => this.fail(this.asError(error)));
        for (const listener of this.rpcListeners) listener(frame.data.slice());
      }
    }
  }

  private requireOkStatus(frame: ExpansionFrame): void {
    if (frame.type !== ExpansionFrameType.Status) {
      throw new Error(`Expected expansion status, received frame ${frame.type}`);
    }
    if (frame.error !== 0) {
      throw new Error(`Wi-Fi board ${this.board.id} expansion request failed with status ${frame.error}`);
    }
  }

  private socketClosed(error?: Error): void {
    if (this.state !== "closing" && this.state !== "closed") {
      this.fail(error ?? new Error(`Wi-Fi board ${this.board.id} TCP socket closed`));
    }
  }

  private fail(error: Error): void {
    if (this.state === "closed" || this.state === "closing") return;
    if (this.disconnectError) return;
    this.state = "error";
    this.disconnectError = error;
    this.cancelHeartbeat?.();
    this.cancelHeartbeat = undefined;
    this.rejectWaiters(error);
    for (const listener of this.disconnectListeners) listener(error);
  }

  private rejectWaiter(type: ExpansionFrameType, error: Error): void {
    const index = this.waiters.findIndex((waiter) => waiter.type === type);
    if (index < 0) return;
    const [waiter] = this.waiters.splice(index, 1);
    clearTimeout(waiter.timer);
    waiter.reject(error);
  }

  private rejectWaiters(error: Error): void {
    for (const waiter of this.waiters.splice(0)) {
      clearTimeout(waiter.timer);
      waiter.reject(error);
    }
  }

  private detachSocket(): void {
    this.removeDataListener?.();
    this.removeCloseListener?.();
    this.removeDataListener = undefined;
    this.removeCloseListener = undefined;
    this.socket = undefined;
  }

  private asError(error: unknown): Error {
    return error instanceof Error ? error : new Error(String(error));
  }
}

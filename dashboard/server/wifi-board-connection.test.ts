import { describe, expect, it } from "vitest";
import {
  type BoardConnectionIo,
  type ByteSocket,
  WifiBoardConnection,
} from "./wifi-board-connection";
import {
  ExpansionControlCommand,
  ExpansionDecoder,
  ExpansionFrameType,
  encodeData,
  encodeHeartbeat,
  encodeStatus,
} from "./expansion";

class TestSocket implements ByteSocket {
  readonly writes: Uint8Array[] = [];
  closed = false;
  private dataListener: (data: Uint8Array) => void = () => undefined;
  private closeListener: (error?: Error) => void = () => undefined;
  private readonly decoder = new ExpansionDecoder();

  async write(data: Uint8Array): Promise<void> {
    this.writes.push(data.slice());
    if (data.byteLength === 1 && data[0] === 0x00) {
      queueMicrotask(() => this.receive(encodeHeartbeat()));
      return;
    }

    for (const frame of this.decoder.push(data)) {
      if (
        frame.type === ExpansionFrameType.BaudRate ||
        frame.type === ExpansionFrameType.Control ||
        frame.type === ExpansionFrameType.Data
      ) {
        queueMicrotask(() => this.receive(encodeStatus(0)));
      } else if (frame.type === ExpansionFrameType.Heartbeat) {
        queueMicrotask(() => this.receive(encodeHeartbeat()));
      }
    }
  }

  onData(listener: (data: Uint8Array) => void): () => void {
    this.dataListener = listener;
    return () => {
      this.dataListener = () => undefined;
    };
  }

  onClose(listener: (error?: Error) => void): () => void {
    this.closeListener = listener;
    return () => {
      this.closeListener = () => undefined;
    };
  }

  async close(): Promise<void> {
    this.closed = true;
    this.closeListener();
  }

  receive(data: Uint8Array): void {
    this.dataListener(data);
  }

  disconnect(error: Error): void {
    this.closeListener(error);
  }
}

class TestIo implements BoardConnectionIo {
  readonly socket = new TestSocket();
  readonly uartConfigs: Array<{ bitRate: number; stopBits: number; parity: number; dataBits: number }> = [];
  readonly delays: number[] = [];
  readonly scheduled: Array<{ intervalMs: number; callback: () => void }> = [];

  async setUartConfig(
    _boardUrl: URL,
    config: { bitRate: number; stopBits: number; parity: number; dataBits: number },
  ): Promise<void> {
    this.uartConfigs.push(config);
  }

  async connectTcp(host: string, port: number): Promise<ByteSocket> {
    expect(host).toBe("blackmagic.local");
    expect(port).toBe(3456);
    return this.socket;
  }

  async delay(milliseconds: number): Promise<void> {
    this.delays.push(milliseconds);
  }

  scheduleRepeating(callback: () => void, intervalMs: number): () => void {
    this.scheduled.push({ callback, intervalMs });
    return () => undefined;
  }
}

describe("single-owner Wi-Fi board connection", () => {
  it("triggers detection, negotiates both UART ends, and starts RPC", async () => {
    const io = new TestIo();
    const connection = new WifiBoardConnection(
      { id: "field", httpUrl: new URL("http://blackmagic.local"), tcpPort: 3456 },
      io,
    );

    await connection.connect();

    expect(connection.state).toBe("ready");
    expect(io.uartConfigs).toEqual([
      { bitRate: 9_600, stopBits: 0, parity: 0, dataBits: 8 },
      { bitRate: 230_400, stopBits: 0, parity: 0, dataBits: 8 },
    ]);
    expect(io.delays).toEqual([25]);
    expect(io.scheduled).toHaveLength(1);
    expect(io.scheduled[0].intervalMs).toBeLessThan(250);

    expect(io.socket.writes[0]).toEqual(Uint8Array.of(0x00));
    const frames = new ExpansionDecoder().push(
      Uint8Array.from(io.socket.writes.slice(1).flatMap((write) => [...write])),
    );
    expect(frames[0]).toEqual({ type: ExpansionFrameType.BaudRate, baud: 230_400 });
    expect(frames[1]).toEqual({
      type: ExpansionFrameType.Control,
      command: ExpansionControlCommand.StartRpc,
    });
  });

  it("frames RPC bytes with backpressure and acknowledges device responses", async () => {
    const io = new TestIo();
    const connection = new WifiBoardConnection(
      { id: "field", httpUrl: new URL("http://blackmagic.local"), tcpPort: 3456 },
      io,
    );
    await connection.connect();
    const beforeRequest = io.socket.writes.length;

    await connection.sendRpc(Uint8Array.from({ length: 130 }, (_, index) => index));

    const requestFrames = io.socket.writes
      .slice(beforeRequest)
      .flatMap((write) => new ExpansionDecoder().push(write))
      .filter((frame) => frame.type === ExpansionFrameType.Data);
    expect(requestFrames.map((frame) => frame.data.byteLength)).toEqual([64, 64, 2]);

    const received: Uint8Array[] = [];
    connection.onRpcData((data) => received.push(data));
    io.socket.receive(encodeData(Uint8Array.of(0xde, 0xad, 0xbe, 0xef)));
    await Promise.resolve();

    expect(received).toEqual([Uint8Array.of(0xde, 0xad, 0xbe, 0xef)]);
    expect(io.socket.writes.at(-1)).toEqual(encodeStatus(0));
  });

  it("keeps the expansion session alive and stops RPC before closing", async () => {
    const io = new TestIo();
    const connection = new WifiBoardConnection(
      { id: "field", httpUrl: new URL("http://blackmagic.local"), tcpPort: 3456 },
      io,
    );
    await connection.connect();

    io.scheduled[0].callback();
    await Promise.resolve();
    await Promise.resolve();
    expect(io.socket.writes.at(-1)).toEqual(encodeHeartbeat());

    await connection.close();
    expect(io.socket.closed).toBe(true);
    const finalControl = new ExpansionDecoder().push(io.socket.writes.at(-1) ?? new Uint8Array());
    expect(finalControl).toEqual([{
      type: ExpansionFrameType.Control,
      command: ExpansionControlCommand.StopRpc,
    }]);
  });

  it("reports a dropped board socket exactly once", async () => {
    const io = new TestIo();
    const connection = new WifiBoardConnection(
      { id: "field", httpUrl: new URL("http://blackmagic.local"), tcpPort: 3456 },
      io,
    );
    await connection.connect();
    const failures: Error[] = [];
    connection.onDisconnect((error) => failures.push(error));

    io.socket.disconnect(new Error("board Wi-Fi disappeared"));
    io.socket.disconnect(new Error("duplicate close notification"));

    expect(connection.state).toBe("error");
    expect(failures.map((error) => error.message)).toEqual(["board Wi-Fi disappeared"]);
    await expect(connection.sendRpc(Uint8Array.of(1))).rejects.toThrow(/not ready/i);
  });
});

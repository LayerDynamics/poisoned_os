import { describe, expect, it } from "vitest";
import { WifiControlPlane, type BoardRpcConnection } from "./wifi-control-plane";
import type { RuntimeBoardConfig } from "./runtime-config";

class TestConnection implements BoardRpcConnection {
  state = "disconnected";
  readonly sent: Uint8Array[] = [];
  closed = false;
  private readonly listeners = new Set<(data: Uint8Array) => void>();
  private readonly disconnectListeners = new Set<(error: Error) => void>();

  async connect(): Promise<void> {
    this.state = "ready";
  }

  async sendRpc(data: Uint8Array): Promise<void> {
    this.sent.push(data.slice());
  }

  onRpcData(listener: (data: Uint8Array) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  onDisconnect(listener: (error: Error) => void): () => void {
    this.disconnectListeners.add(listener);
    return () => this.disconnectListeners.delete(listener);
  }

  async close(): Promise<void> {
    this.closed = true;
    this.state = "closed";
  }

  receive(data: Uint8Array): void {
    for (const listener of this.listeners) listener(data);
  }

  disconnect(error: Error): void {
    this.state = "error";
    for (const listener of this.disconnectListeners) listener(error);
  }
}

const boards: RuntimeBoardConfig[] = [
  { id: "field", httpUrl: new URL("http://blackmagic.local"), tcpPort: 3456 },
  { id: "lab", httpUrl: new URL("http://192.168.4.44"), tcpPort: 3456 },
];

describe("Wi-Fi control-plane ownership", () => {
  it("allows one opaque RPC stream per board and never opens a second raw UART owner", async () => {
    const created = new Map<string, TestConnection[]>();
    const controlPlane = new WifiControlPlane(boards, (board) => {
      const connection = new TestConnection();
      created.set(board.id, [...(created.get(board.id) ?? []), connection]);
      return connection;
    });

    const first = await controlPlane.openSession("field", "browser-a");
    await expect(controlPlane.openSession("field", "browser-b")).rejects.toThrow(/already in use/i);
    expect(created.get("field")).toHaveLength(1);

    const browserBytes: Uint8Array[] = [];
    first.onData((data) => browserBytes.push(data));
    await first.send(Uint8Array.of(0x08, 0x96, 0x01));
    created.get("field")?.[0].receive(Uint8Array.of(0x10, 0x01));
    expect(created.get("field")?.[0].sent).toEqual([Uint8Array.of(0x08, 0x96, 0x01)]);
    expect(browserBytes).toEqual([Uint8Array.of(0x10, 0x01)]);

    const secondBoard = await controlPlane.openSession("lab", "browser-b");
    expect(created.get("lab")).toHaveLength(1);
    await secondBoard.close();
    await first.close();

    const resumed = await controlPlane.openSession("field", "browser-a-resume");
    expect(created.get("field")).toHaveLength(2);
    await resumed.close();
  });

  it("rejects unknown boards without constructing a connection", async () => {
    let constructed = false;
    const controlPlane = new WifiControlPlane(boards, () => {
      constructed = true;
      return new TestConnection();
    });

    await expect(controlPlane.openSession("missing", "browser-a")).rejects.toThrow(/unknown/i);
    expect(constructed).toBe(false);
  });

  it("releases ownership and closes the session when the Wi-Fi board disappears", async () => {
    const created: TestConnection[] = [];
    const controlPlane = new WifiControlPlane(boards, () => {
      const connection = new TestConnection();
      created.push(connection);
      return connection;
    });
    const session = await controlPlane.openSession("field", "browser-a");
    const failures: Error[] = [];
    session.onDisconnect((error) => failures.push(error));

    created[0].disconnect(new Error("board TCP connection lost"));
    await Promise.resolve();

    expect(failures.map((error) => error.message)).toEqual(["board TCP connection lost"]);
    expect(controlPlane.statuses().find((board) => board.id === "field")?.state).toBe("available");
    await expect(session.send(Uint8Array.of(1))).rejects.toThrow(/closed/i);
    const replacement = await controlPlane.openSession("field", "browser-b");
    expect(created).toHaveLength(2);
    await replacement.close();
  });
});

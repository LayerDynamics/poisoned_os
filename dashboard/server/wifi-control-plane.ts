import type { RuntimeBoardConfig } from "./runtime-config";
import { WifiBoardConnection } from "./wifi-board-connection";

export interface BoardRpcConnection {
  readonly state: string;
  connect(): Promise<void>;
  sendRpc(data: Uint8Array): Promise<void>;
  onRpcData(listener: (data: Uint8Array) => void): () => void;
  onDisconnect(listener: (error: Error) => void): () => void;
  close(): Promise<void>;
}

export interface WifiRpcSession {
  readonly boardId: string;
  readonly clientId: string;
  send(data: Uint8Array): Promise<void>;
  onData(listener: (data: Uint8Array) => void): () => void;
  onDisconnect(listener: (error: Error) => void): () => void;
  close(): Promise<void>;
}

export interface WifiBoardStatus {
  id: string;
  label: string;
  state: "available" | "connecting" | "in-use";
}

type ConnectionFactory = (board: RuntimeBoardConfig) => BoardRpcConnection;

function boardLabel(id: string): string {
  return id
    .split("-")
    .filter(Boolean)
    .map((part) => `${part[0]?.toUpperCase() ?? ""}${part.slice(1)}`)
    .join(" ") + " Flipper";
}

export class WifiControlPlane {
  private readonly boards = new Map<string, RuntimeBoardConfig>();
  private readonly owners = new Map<string, { clientId: string; state: "connecting" | "in-use" }>();

  constructor(
    boards: readonly RuntimeBoardConfig[],
    private readonly createConnection: ConnectionFactory = (board) => new WifiBoardConnection(board),
  ) {
    for (const board of boards) {
      if (this.boards.has(board.id)) throw new Error(`Duplicate Wi-Fi board id ${board.id}`);
      this.boards.set(board.id, board);
    }
  }

  statuses(): WifiBoardStatus[] {
    return [...this.boards.values()].map((board) => ({
      id: board.id,
      label: boardLabel(board.id),
      state: this.owners.get(board.id)?.state ?? "available",
    }));
  }

  async openSession(boardId: string, clientId: string): Promise<WifiRpcSession> {
    const board = this.boards.get(boardId);
    if (!board) throw new Error(`Unknown Wi-Fi board ${boardId}`);
    if (this.owners.has(boardId)) throw new Error(`Wi-Fi board ${boardId} is already in use`);
    if (clientId.length < 1 || clientId.length > 128) throw new Error("Wi-Fi client id is invalid");

    this.owners.set(boardId, { clientId, state: "connecting" });
    const connection = this.createConnection(board);
    try {
      await connection.connect();
    } catch (error) {
      this.releaseOwner(boardId, clientId);
      throw error;
    }
    if (connection.state !== "ready") {
      this.releaseOwner(boardId, clientId);
      await connection.close().catch(() => undefined);
      throw new Error(`Wi-Fi board ${boardId} did not reach the ready state`);
    }
    this.owners.set(boardId, { clientId, state: "in-use" });

    let closed = false;
    let disconnectError: Error | undefined;
    const disconnectListeners = new Set<(error: Error) => void>();
    const removeConnectionDisconnect = connection.onDisconnect((error) => {
      if (closed) return;
      closed = true;
      disconnectError = error;
      this.releaseOwner(boardId, clientId);
      for (const listener of disconnectListeners) listener(error);
      void connection.close().catch(() => undefined);
    });
    const close = async () => {
      if (closed) return;
      closed = true;
      removeConnectionDisconnect();
      try {
        await connection.close();
      } finally {
        this.releaseOwner(boardId, clientId);
      }
    };

    return {
      boardId,
      clientId,
      send: (data) => {
        if (closed) return Promise.reject(new Error(`Wi-Fi board session ${boardId} is closed`));
        return connection.sendRpc(data);
      },
      onData: (listener) => {
        if (closed) throw new Error(`Wi-Fi board session ${boardId} is closed`);
        return connection.onRpcData(listener);
      },
      onDisconnect: (listener) => {
        disconnectListeners.add(listener);
        if (disconnectError) {
          const error = disconnectError;
          queueMicrotask(() => {
            if (disconnectListeners.has(listener)) listener(error);
          });
        }
        return () => disconnectListeners.delete(listener);
      },
      close,
    };
  }

  private releaseOwner(boardId: string, clientId: string): void {
    if (this.owners.get(boardId)?.clientId === clientId) this.owners.delete(boardId);
  }
}

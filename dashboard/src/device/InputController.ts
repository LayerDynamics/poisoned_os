export type InputKey = "up" | "down" | "left" | "right" | "ok" | "back";
export type InputType = "press" | "release" | "short" | "long" | "repeat";
export interface InputEvent { key: InputKey; type: InputType; atMs: number; }

export class InputController {
  private readonly held = new Set<InputKey>();
  private closed = false;
  public constructor(private readonly send: (event: InputEvent) => Promise<void>) {}

  public async press(key: InputKey, atMs: number): Promise<void> { await this.emit({ key, type: "press", atMs }); this.held.add(key); }
  public async release(key: InputKey, atMs: number): Promise<void> { await this.emit({ key, type: "release", atMs }); this.held.delete(key); }
  public async short(key: InputKey, atMs: number): Promise<void> { await this.emit({ key, type: "short", atMs }); }
  public async long(key: InputKey, atMs: number): Promise<void> { await this.emit({ key, type: "long", atMs }); }
  public async repeat(key: InputKey, atMs: number): Promise<void> { await this.emit({ key, type: "repeat", atMs }); }
  public async close(atMs: number): Promise<void> {
    if (this.closed) return;
    try {
      for (const key of this.held) await this.emit({ key, type: "release", atMs });
    } finally {
      this.closed = true;
      this.held.clear();
    }
  }
  public get heldKeys(): readonly InputKey[] { return [...this.held]; }
  private async emit(event: InputEvent): Promise<void> {
    if (this.closed) throw new Error("input controller is closed");
    if (!Number.isSafeInteger(event.atMs) || event.atMs < 0) throw new Error("invalid input timestamp");
    await this.send(event);
  }
}

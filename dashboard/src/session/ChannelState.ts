export const MAX_FRAME_BYTES = 1024;
export const MAX_CREDITS = 4;

export type ReceiveResult = "accepted" | "duplicate" | "gap";

export class ChannelStateError extends Error {
  constructor(public readonly code: "closed" | "invalid" | "no-credit" | "sequence-wrap") {
    super(code);
    this.name = "ChannelStateError";
  }
}

export class ChannelState {
  private active = true;
  private nextTransmit = 0n;
  private nextReceive = 0n;

  public constructor(
    public readonly name: string,
    private credits: number,
  ) {
    if (!name || name.length > 32 || !Number.isInteger(credits) || credits < 0 || credits > MAX_CREDITS) {
      throw new ChannelStateError("invalid");
    }
  }

  public reserveSend(frameBytes: number): bigint {
    this.ensureActive();
    if (!Number.isInteger(frameBytes) || frameBytes < 0 || frameBytes > MAX_FRAME_BYTES) {
      throw new ChannelStateError("invalid");
    }
    if (this.credits === 0) throw new ChannelStateError("no-credit");
    if (this.nextTransmit === 0xffffffffffffffffn) throw new ChannelStateError("sequence-wrap");
    const sequence = this.nextTransmit;
    this.nextTransmit += 1n;
    this.credits -= 1;
    return sequence;
  }

  public receive(frameBytes: number, sequence: bigint): ReceiveResult {
    this.ensureActive();
    if (!Number.isInteger(frameBytes) || frameBytes < 0 || frameBytes > MAX_FRAME_BYTES || sequence < 0n) {
      throw new ChannelStateError("invalid");
    }
    if (sequence < this.nextReceive) return "duplicate";
    if (sequence > this.nextReceive) return "gap";
    if (this.nextReceive === 0xffffffffffffffffn) throw new ChannelStateError("sequence-wrap");
    this.nextReceive += 1n;
    return "accepted";
  }

  public addCredits(credits: number): void {
    this.ensureActive();
    if (!Number.isInteger(credits) || credits < 0 || credits > MAX_CREDITS - this.credits) {
      throw new ChannelStateError("invalid");
    }
    this.credits += credits;
  }

  public close(): void {
    this.active = false;
  }

  public get availableCredits(): number {
    return this.credits;
  }

  public get receiveSequence(): bigint {
    return this.nextReceive;
  }

  public get transmitSequence(): bigint {
    return this.nextTransmit;
  }

  private ensureActive(): void {
    if (!this.active) throw new ChannelStateError("closed");
  }
}

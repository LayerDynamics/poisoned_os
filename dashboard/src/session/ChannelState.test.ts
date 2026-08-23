import { describe, expect, it } from "vitest";

import { ChannelState, ChannelStateError } from "./ChannelState";

describe("ChannelState", () => {
  it("enforces credits and identifies duplicate and gap frames", () => {
    const channel = new ChannelState("device", 1);
    expect(channel.reserveSend(32)).toBe(0n);
    expect(channel.transmitSequence).toBe(1n);
    expect(() => channel.reserveSend(32)).toThrowError(
      new ChannelStateError("no-credit"),
    );
    expect(channel.receive(32, 1n)).toBe("gap");
    expect(channel.receive(32, 0n)).toBe("accepted");
    expect(channel.receive(32, 0n)).toBe("duplicate");
  });

  it("rejects frames above the RPC buffer and closes deterministically", () => {
    const channel = new ChannelState("device", 0);
    expect(() => channel.receive(1025, 0n)).toThrowError(
      new ChannelStateError("invalid"),
    );
    channel.close();
    expect(() => channel.receive(1, 0n)).toThrowError(
      new ChannelStateError("closed"),
    );
  });
});

import { describe, expect, it } from "vitest";
import {
  ExpansionDecoder,
  ExpansionFrameType,
  encodeBaudRate,
  encodeControl,
  encodeData,
  encodeHeartbeat,
  encodeStatus,
} from "./expansion";

describe("Flipper expansion protocol", () => {
  it("encodes the firmware wire format and XOR checksum", () => {
    expect(encodeHeartbeat()).toEqual(Uint8Array.from([0x01, 0x01]));
    expect(encodeStatus(0)).toEqual(Uint8Array.from([0x02, 0x00, 0x02]));
    expect(encodeBaudRate(230_400)).toEqual(
      Uint8Array.from([0x03, 0x00, 0x84, 0x03, 0x00, 0x84]),
    );
    expect(encodeControl(0)).toEqual(Uint8Array.from([0x04, 0x00, 0x04]));
    expect(encodeData(Uint8Array.from([0xaa, 0xbb, 0xcc]))).toEqual(
      Uint8Array.from([0x05, 0x03, 0xaa, 0xbb, 0xcc, 0xdb]),
    );
  });

  it("decodes fragmented and coalesced frames without losing boundaries", () => {
    const decoder = new ExpansionDecoder();
    expect(decoder.push(Uint8Array.from([0x01]))).toEqual([]);
    expect(decoder.push(Uint8Array.from([0x01, 0x02, 0x00]))).toEqual([
      { type: ExpansionFrameType.Heartbeat },
    ]);
    expect(decoder.push(Uint8Array.from([0x02, 0x05, 0x02, 0x10, 0x20, 0x37]))).toEqual([
      { type: ExpansionFrameType.Status, error: 0 },
      { type: ExpansionFrameType.Data, data: Uint8Array.from([0x10, 0x20]) },
    ]);
  });

  it("rejects malformed, oversized, and corrupt frames", () => {
    const invalidType = new ExpansionDecoder();
    expect(() => invalidType.push(Uint8Array.from([0xff, 0xff]))).toThrow(/frame type/i);

    const oversized = new ExpansionDecoder();
    expect(() => oversized.push(Uint8Array.from([0x05, 65]))).toThrow(/64 bytes/i);

    const corrupt = new ExpansionDecoder();
    expect(() => corrupt.push(Uint8Array.from([0x02, 0x00, 0xff]))).toThrow(/checksum/i);
  });
});

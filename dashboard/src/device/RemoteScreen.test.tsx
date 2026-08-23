import { describe, expect, it } from "vitest";
import { acceptScreenFrame, renderScreenFrame, ScreenFrameError } from "./RemoteScreen";

const frame = (sequence: bigint, size = 1024) => ({ sequence, data: new Uint8Array(size), orientation: "horizontal" as const, receivedAtMs: 1 });
describe("RemoteScreen", () => {
  it("rejects stale and malformed frames", () => {
    expect(acceptScreenFrame(null, frame(1n)).sequence).toBe(1n);
    expect(() => acceptScreenFrame(frame(2n), frame(2n))).toThrowError(new ScreenFrameError("stale", "stale screen frame"));
    expect(() => acceptScreenFrame(null, frame(1n, 10))).toThrowError();
  });

  it("renders packed framebuffer bits and vertical orientation", () => {
    const data = new Uint8Array(1024);
    data[0] = 1;
    const horizontal = renderScreenFrame({ ...frame(1n), data });
    expect([horizontal.width, horizontal.height]).toEqual([128, 64]);
    expect([...horizontal.rgba.slice(0, 4)]).toEqual([16, 20, 24, 255]);
    expect([...horizontal.rgba.slice(4, 8)]).toEqual([255, 149, 0, 255]);
    const vertical = renderScreenFrame({ ...frame(1n), data, orientation: "vertical" });
    expect([vertical.width, vertical.height]).toEqual([64, 128]);
    const rotatedPixel = 63 * 4;
    expect([...vertical.rgba.slice(rotatedPixel, rotatedPixel + 4)]).toEqual([16, 20, 24, 255]);
  });
});

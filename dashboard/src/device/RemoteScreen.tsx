import { useEffect, useRef, type ReactElement } from "react";

export type ScreenOrientation = "horizontal" | "horizontal-flip" | "vertical" | "vertical-flip";
export interface ScreenFrame { sequence: bigint; data: Uint8Array; orientation: ScreenOrientation; receivedAtMs: number; }

export class ScreenFrameError extends Error { public constructor(public readonly code: "stale" | "invalid" | "oversize", message: string) { super(message); this.name = "ScreenFrameError"; } }

export function acceptScreenFrame(previous: ScreenFrame | null, next: ScreenFrame): ScreenFrame {
  if (next.data.byteLength !== 1024) throw new ScreenFrameError("invalid", "screen frame must be 128x64 monochrome bytes");
  if (next.sequence < 0n || !Number.isSafeInteger(next.receivedAtMs) || next.receivedAtMs < 0) throw new ScreenFrameError("invalid", "invalid frame metadata");
  if (previous && next.sequence <= previous.sequence) throw new ScreenFrameError("stale", "stale screen frame");
  return next;
}

export interface RenderedScreenFrame { width: number; height: number; rgba: Uint8ClampedArray; }

export function renderScreenFrame(frame: ScreenFrame): RenderedScreenFrame {
  acceptScreenFrame(null, frame);
  const vertical = frame.orientation === "vertical" || frame.orientation === "vertical-flip";
  const width = vertical ? 64 : 128;
  const height = vertical ? 128 : 64;
  const rgba = new Uint8ClampedArray(width * height * 4);
  const sourcePixel = (x: number, y: number): boolean =>
    (frame.data[x + Math.floor(y / 8) * 128] & (1 << (y % 8))) !== 0;
  for (let outputY = 0; outputY < height; outputY += 1) {
    for (let outputX = 0; outputX < width; outputX += 1) {
      let sourceX = outputX;
      let sourceY = outputY;
      if (frame.orientation === "horizontal-flip") {
        sourceX = 127 - outputX;
        sourceY = 63 - outputY;
      } else if (frame.orientation === "vertical") {
        sourceX = outputY;
        sourceY = 63 - outputX;
      } else if (frame.orientation === "vertical-flip") {
        sourceX = 127 - outputY;
        sourceY = outputX;
      }
      const active = sourcePixel(sourceX, sourceY);
      const offset = (outputY * width + outputX) * 4;
      rgba[offset] = active ? 16 : 255;
      rgba[offset + 1] = active ? 20 : 149;
      rgba[offset + 2] = active ? 24 : 0;
      rgba[offset + 3] = 255;
    }
  }
  return { width, height, rgba };
}

export function RemoteScreen({ frame }: { frame: ScreenFrame | null }): ReactElement {
  const canvas = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    if (!frame || !canvas.current) return;
    const rendered = renderScreenFrame(frame);
    canvas.current.width = rendered.width;
    canvas.current.height = rendered.height;
    const context = canvas.current.getContext("2d");
    if (!context) return;
    context.imageSmoothingEnabled = false;
    const pixels = new Uint8ClampedArray(rendered.rgba);
    context.putImageData(new ImageData(pixels, rendered.width, rendered.height), 0, 0);
  }, [frame]);
  const vertical = frame?.orientation === "vertical" || frame?.orientation === "vertical-flip";
  return <section aria-label="Remote screen">
    <canvas ref={canvas} width={vertical ? 64 : 128} height={vertical ? 128 : 64} aria-label={frame ? `Frame ${frame.sequence}` : "No frame"} />
  </section>;
}

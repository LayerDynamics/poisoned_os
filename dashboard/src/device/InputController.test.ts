import { describe, expect, it } from "vitest";
import { InputController } from "./InputController";

describe("InputController", () => {
  it("preserves press/release order and releases held keys on disconnect", async () => {
    const events: string[] = [];
    const controller = new InputController(async (event) => { events.push(`${event.key}:${event.type}`); });
    await controller.press("ok", 1);
    await controller.long("ok", 2);
    await controller.close(3);
    expect(events).toEqual(["ok:press", "ok:long", "ok:release"]);
    expect(controller.heldKeys).toEqual([]);
    await expect(controller.release("ok", 4)).rejects.toThrow();
  });
});

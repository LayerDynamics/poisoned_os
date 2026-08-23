import { describe, expect, it } from "vitest";
import { BrowserIdentityStore } from "./BrowserIdentityStore";

describe("BrowserIdentityStore", () => {
  it("reuses one non-extractable signing identity when persistent browser storage is unavailable", async () => {
    const store = new BrowserIdentityStore();
    const first = await store.getOrCreate();
    const second = await store.getOrCreate();

    expect(second.privateKey).toBe(first.privateKey);
    expect(second.publicKey).toBe(first.publicKey);
    expect(first.privateKey.extractable).toBe(false);
    expect(new Uint8Array(await crypto.subtle.exportKey("raw", first.publicKey))).toHaveLength(65);
  });
});

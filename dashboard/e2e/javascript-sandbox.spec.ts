import { expect, test } from "@playwright/test";

const wifiBoardId = process.env.POISON_HIL_WIFI_BOARD_ID ?? "";
const bundleId = process.env.POISON_HIL_UI_BUNDLE_ID ?? "";
const bundleVersion = process.env.POISON_HIL_UI_BUNDLE_VERSION ?? "";
const bundleSha256 = process.env.POISON_HIL_UI_BUNDLE_SHA256 ?? "";
const physicalInputsPresent = /^[a-z][a-z0-9-]{0,62}$/.test(wifiBoardId) &&
  /^[a-z0-9][a-z0-9._-]{0,63}$/.test(bundleId) && /^\d+(?:\.\d+){0,2}$/.test(bundleVersion) &&
  /^[0-9a-f]{64}$/.test(bundleSha256);

test.describe("physical JavaScript sandbox", () => {
  test.skip(!physicalInputsPresent, "requires an explicit physical Wi-Fi board and installed signed ui-pack");

  test("streams device bytes and fails every browser escape closed", async ({ page }) => {
    const query = new URLSearchParams({
      wifiBoardId,
      servedBundleId: bundleId,
      servedBundleVersion: bundleVersion,
      servedBundleSha256: bundleSha256,
    });
    await page.goto(`/?${query.toString()}`);
    await page.getByRole("button", { name: "Connect over Wi-Fi" }).click();
    const pairing = page.getByRole("dialog", { name: "Match this code on the device" });
    if (await pairing.isVisible({ timeout: 2_000 }).catch(() => false)) {
      await pairing.getByRole("button", { name: "Code matches" }).click();
    }
    await expect(page.getByText("Encrypted RPC ping verified. Device control is active.")).toBeVisible({ timeout: 60_000 });
    const iframe = page.locator('iframe[title="Device interface"]');
    await expect(iframe).toHaveAttribute("sandbox", "allow-scripts", { timeout: 60_000 });
    const frame = page.frames().find((candidate) => candidate !== page.mainFrame() && candidate.url().includes(bundleSha256));
    expect(frame, "verified content-addressed device iframe must load").toBeTruthy();
    if (!frame) return;

    const probes = await frame.evaluate(async () => {
      const denied = async (operation: () => unknown | Promise<unknown>): Promise<boolean> => {
        try { await operation(); return false; } catch { return true; }
      };
      const timedFetch = async (url: string): Promise<void> => {
        const abort = new AbortController();
        const timer = setTimeout(() => abort.abort(), 500);
        try { await fetch(url, { signal: abort.signal, credentials: "include" }); }
        finally { clearTimeout(timer); }
      };
      return {
        opaqueOrigin: location.origin === "null",
        parentDom: await denied(() => window.parent.document.body),
        topDom: await denied(() => window.top?.document.body),
        cookies: await denied(() => { document.cookie = "escape=1"; return document.cookie; }),
        localStorage: await denied(() => localStorage.setItem("escape", "1")),
        sessionStorage: await denied(() => sessionStorage.setItem("escape", "1")),
        indexedDb: await denied(() => new Promise<void>((resolve, reject) => {
          const request = indexedDB.open("escape");
          request.onsuccess = () => resolve();
          request.onerror = () => reject(request.error);
        })),
        cacheApi: await denied(() => caches.open("escape")),
        serviceWorker: !navigator.serviceWorker || await denied(() => navigator.serviceWorker.register("worker.js")),
        broadcastChannel: await denied(() => { const channel = new BroadcastChannel("escape"); channel.close(); }),
        sharedWorker: await denied(() => { const worker = new SharedWorker("worker.js"); worker.port.close(); }),
        wanFetch: await denied(() => timedFetch("https://example.com/")),
        runtimeFetch: await denied(() => timedFetch("http://127.0.0.1:4173/api/runtime/v1/manifest")),
        webSocket: await denied(() => { const socket = new WebSocket("ws://127.0.0.1:4173/api/runtime/v1/boards/primary/rpc"); socket.close(); }),
        topNavigation: await denied(() => { if (window.top) window.top.location.href = "https://example.com/"; }),
        popup: window.open("https://example.com/") === null,
        clipboard: !navigator.clipboard || await denied(() => navigator.clipboard.readText()),
      };
    });
    expect(probes).toEqual(Object.fromEntries(Object.keys(probes).map((key) => [key, true])));

    const allowed = await frame.evaluate(async () => {
      const nonce = document.querySelector<HTMLMetaElement>('meta[name="poison-broker-nonce"]')?.content;
      if (!nonce) throw new Error("broker nonce is missing");
      const response = new Promise<unknown>((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error("broker response timed out")), 10_000);
        addEventListener("message", (event) => {
          const data = event.data as { type?: string; nonce?: string; sequence?: number; ok?: boolean; result?: unknown; error?: string };
          if (data.type !== "poison.response" || data.nonce !== nonce || data.sequence !== 0) return;
          clearTimeout(timer);
          if (data.ok) resolve(data.result); else reject(new Error(data.error ?? "broker denied"));
        }, { once: true });
      });
      parent.postMessage({
        type: "poison.request",
        nonce,
        sequence: 0,
        capability: "device.status.read",
        operation: "read",
        payload: {},
      }, "*");
      return response;
    });
    expect(allowed).toMatchObject({ firmwareVersion: expect.any(String), batteryPercent: expect.any(Number) });

    const forgedResponses = await frame.evaluate(async () => {
      const nonce = document.querySelector<HTMLMetaElement>('meta[name="poison-broker-nonce"]')?.content ?? "";
      let responses = 0;
      const listener = (event: MessageEvent) => {
        if ((event.data as { type?: string }).type === "poison.response") responses += 1;
      };
      addEventListener("message", listener);
      parent.postMessage({ type: "poison.request", nonce, sequence: 0, capability: "device.status.read", operation: "read", payload: {} }, "*");
      parent.postMessage({ type: "poison.request", nonce: "forged", sequence: 1, capability: "device.status.read", operation: "read", payload: {} }, "*");
      await new Promise((resolve) => setTimeout(resolve, 500));
      removeEventListener("message", listener);
      return responses;
    });
    expect(forgedResponses).toBe(0);
  });
});

import { expect, test } from "@playwright/test";

const wifiBoardId = process.env.POISON_HIL_WIFI_BOARD_ID ?? "";

test.describe("dashboard shell", () => {
  test("loads without installing a mock transport", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator("body")).toContainText(/Poisoned_Os|Dashboard/i);
    await expect(page.locator("body")).not.toContainText(/mock transport|simulated device/i);
  });

  test("@physical-pair-control pairs and verifies the exact Wi-Fi-board route", async ({ page }) => {
    test.skip(!/^[a-z][a-z0-9-]{0,62}$/.test(wifiBoardId), "requires an explicit physical Wi-Fi board id");
    await page.goto(`/?wifiBoardId=${encodeURIComponent(wifiBoardId)}`);
    await page.getByRole("button", { name: "Connect over Wi-Fi" }).click();
    const pairing = page.getByRole("dialog", { name: "Match this code on the device" });
    await expect(pairing).toBeVisible({ timeout: 30_000 });
    await pairing.getByRole("button", { name: "Code matches" }).click();
    await expect(page.getByText("Encrypted RPC ping verified. Device control is active."))
      .toBeVisible({ timeout: 90_000 });
    await expect(page.getByText("HTTP(S) dashboard · WS(S) RPC · local Node.js runtime · Wi-Fi board"))
      .toBeVisible();
  });
});

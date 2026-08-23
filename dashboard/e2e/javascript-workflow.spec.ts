import { fileURLToPath } from "node:url";
import { expect, test, type Page } from "@playwright/test";

const wifiBoardId = process.env.POISON_HIL_WIFI_BOARD_ID ?? "";
const physicalInputsPresent = /^[a-z][a-z0-9-]{0,62}$/.test(wifiBoardId);
const fixtureRoot = fileURLToPath(new URL("../../tools/hil/fixtures/javascript-workflow/project/", import.meta.url));

async function pairOverWifi(page: Page): Promise<void> {
  await page.getByRole("button", { name: "Connect over Wi-Fi" }).click();
  const pairing = page.getByRole("dialog", { name: "Match this code on the device" });
  await expect(pairing).toBeVisible({ timeout: 30_000 });
  await pairing.getByRole("button", { name: "Code matches" }).click();
  await expect(page.getByText("Encrypted RPC ping verified. Device control is active."))
    .toBeVisible({ timeout: 90_000 });
}

async function runAndVerify(page: Page): Promise<void> {
  const workspace = page.getByRole("region", { name: "JavaScript workspace" });
  await workspace.getByRole("button", { name: "Run", exact: true }).click();
  await expect(workspace.getByLabel("JavaScript run controls").getByRole("status"))
    .toHaveText("completed", { timeout: 90_000 });
  await expect(workspace.getByLabel("JavaScript console")).toContainText("dependency=7");
}

test.describe("physical JavaScript workflow", () => {
  test.skip(!physicalInputsPresent, "requires an explicit physical Wi-Fi board id");

  test("@physical-javascript imports, executes, reconnects, restores, and reruns on the exact device", async ({ page }) => {
    test.setTimeout(300_000);
    await page.goto(`/?wifiBoardId=${encodeURIComponent(wifiBoardId)}`);
    await pairOverWifi(page);

    const workspace = page.getByRole("region", { name: "JavaScript workspace" });
    await workspace.getByLabel("Source for src/main.js").fill(
      'print("dependency=" + require("tiny-value"));\n',
    );
    await workspace.getByLabel("Dependency lock file").setInputFiles(`${fixtureRoot}poison-js.lock`);
    await workspace.getByLabel("Select dependency folder").setInputFiles(`${fixtureRoot}vendor`);
    await workspace.getByRole("button", { name: "Import verified dependencies" }).click();
    await expect(workspace.getByText("Imported 1 immutable dependency files")).toBeVisible();

    await workspace.getByRole("button", { name: "Save revision" }).click();
    await expect(workspace.getByRole("region", { name: "Project revisions" }).getByRole("status"))
      .toContainText("Saved revision 1");
    await runAndVerify(page);
    await workspace.getByLabel("Artifact file").setInputFiles(
      fileURLToPath(new URL("../../tools/hil/fixtures/javascript-workflow/artifacts/report.json", import.meta.url)),
    );
    await workspace.getByRole("button", { name: "Save artifact" }).click();
    await expect(workspace.getByRole("region", { name: "Workload artifacts" }).getByRole("status"))
      .toHaveText("Saved artifact report.json", { timeout: 60_000 });

    await page.getByRole("button", { name: "Disconnect", exact: true }).click();
    await expect(page.getByRole("button", { name: "Connect over Wi-Fi" })).toBeVisible();
    await pairOverWifi(page);

    const restored = page.getByRole("region", { name: "JavaScript workspace" });
    await expect(restored.getByRole("region", { name: "Project revisions" }).getByRole("status"))
      .toContainText("Restored revision 1");
    await expect(restored.getByLabel("Source for src/main.js"))
      .toHaveValue('print("dependency=" + require("tiny-value"));\n');
    await runAndVerify(page);

    await restored.getByLabel("Source for src/main.js").fill(
      'var timers = require("timers");\nvar count = 0;\ntimers.setInterval(function() { print("tick=" + (++count)); }, 20);\n',
    );
    await restored.getByRole("button", { name: "Run", exact: true }).click();
    await expect(restored.getByLabel("JavaScript run controls").getByRole("status"))
      .toHaveText("running", { timeout: 90_000 });
    await expect(restored.getByLabel("JavaScript console"))
      .toContainText("tick=", { timeout: 30_000 });
    await restored.getByRole("button", { name: "Stop", exact: true }).click();
    await expect(restored.getByLabel("JavaScript run controls").getByRole("status"))
      .toHaveText("cancelled", { timeout: 30_000 });
  });
});

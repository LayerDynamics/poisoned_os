import "./style.css";
import { extractUpdateBundle, type UpdateBundle } from "./archive";
import { BrowserFirmwareInstaller, type InstallProgress } from "./installer";
import {
  configuredTrustedKeys,
  downloadRelease,
  loadInstallerRuntimeConfig,
  loadReleaseFeed,
  type InstallerRelease,
} from "./release-feed";
import { serialApi } from "./web-serial";

const root = document.querySelector<HTMLDivElement>("#app");
if (!root) throw new Error("Installer root is missing");

root.innerHTML = `
  <main class="installer-shell" data-brand="poisoned">
    <header class="masthead">
      <a class="wordmark" href="./" aria-label="Poisoned OS cable installer home">
        <span class="wordmark-mark" aria-hidden="true">☣</span>
        <span>Poisoned<span class="wordmark-dim">_Os</span></span>
      </a>
      <nav class="installer-nav" aria-label="Poisoned OS links"><a href="https://poisoned-os-site-production.up.railway.app/">Product site <span aria-hidden="true">↗</span></a><a href="https://poisoned-os-site-production.up.railway.app/docs/">Documentation <span aria-hidden="true">↗</span></a></nav>
      <div class="masthead-meta"><span>Target F7</span><span>USB / 230400</span><span>Local execution</span></div>
    </header>

    <div class="installer-grid">
      <aside class="device-bay" aria-label="Flipper connection status">
        <div class="bay-heading"><p class="kicker">POISONED_OS / INSTALLER</p><span class="bay-index">01—04</span></div>
        <h1>Put Poisoned_Os<br><em>on the device.</em></h1>
        <p class="lede">A local USB install for Flipper Zero target 7. The archive is checked, copied, read back, and verified before the device restarts.</p>
        <div class="brand-stamp" aria-hidden="true"><span>DIRECT FLASH</span><b>F7 / USB</b></div>

        <div class="device-figure" aria-hidden="true">
          <div class="flipper-body" role="img" aria-label="Flipper Zero connection schematic"><span class="device-screen" id="device-screen">NO LINK</span></div>
          <div class="cable-trace"><span id="cable-progress"></span></div>
          <div class="host-port">USB</div>
        </div>

        <dl class="device-facts">
          <div><dt>Device</dt><dd id="fact-device">Not connected</dd></div>
          <div><dt>Current firmware</dt><dd id="fact-firmware">—</dd></div>
          <div><dt>Package</dt><dd id="fact-package">Not selected</dd></div>
          <div><dt>SHA-256</dt><dd id="fact-digest">—</dd></div>
        </dl>
      </aside>

      <section class="workflow" aria-labelledby="workflow-title">
        <div class="tool-rail" aria-label="Installer guarantees"><span class="rail-label">LOCAL PROCESS</span><span class="rail-status"><i></i> NO CLOUD · NO ACCOUNT</span></div>
        <div class="workflow-heading">
          <div><p class="kicker">Install sequence</p><h2 id="workflow-title">Check. Connect. Verify.</h2></div>
          <output id="overall-status" class="status-chip">Ready</output>
        </div>

        <ol class="stage-list">
          <li class="stage active" data-stage="package">
            <div class="stage-index">1</div>
            <div class="stage-body">
              <div class="stage-title"><h3>Choose Poisoned_Os</h3><span id="package-state">Waiting</span></div>
              <p>Use a signed published release or select the official update <code>.tgz</code> yourself.</p>
              <div id="release-panel" class="release-panel" hidden>
                <label for="release-select">Published release</label>
                <div class="control-row">
                  <select id="release-select"></select>
                  <button id="download-release" type="button" class="quiet">Download & verify</button>
                </div>
              </div>
              <label class="file-picker" for="package-file">
                <input id="package-file" type="file" accept=".tgz,application/gzip">
                <span>Select update package</span>
                <small>Processed locally; nothing is uploaded to a server.</small>
              </label>
            </div>
          </li>

          <li class="stage" data-stage="connect">
            <div class="stage-index">2</div>
            <div class="stage-body">
              <div class="stage-title"><h3>Connect the Flipper</h3><span id="connect-state">Waiting</span></div>
              <p>Close qFlipper and other serial tools, unlock the device, then choose the Flipper Zero USB port.</p>
              <button id="connect-device" type="button">Connect over USB</button>
            </div>
          </li>

          <li class="stage" data-stage="review">
            <div class="stage-index">3</div>
            <div class="stage-body">
              <div class="stage-title"><h3>Review the exact target</h3><span id="review-state">Blocked</span></div>
              <p id="review-copy">Select a package and connect a verified Flipper Zero target 7.</p>
              <label class="confirmation"><input id="install-confirmation" type="checkbox" disabled><span>I will keep this USB cable connected until Poisoned_Os is verified.</span></label>
            </div>
          </li>

          <li class="stage" data-stage="install">
            <div class="stage-index">4</div>
            <div class="stage-body">
              <div class="stage-title"><h3>Install and verify</h3><span id="install-state">Waiting</span></div>
              <p id="progress-detail">The installer will copy, read back, start the updater, and wait for the device to return.</p>
              <progress id="install-progress" value="0" max="1"></progress>
              <div class="control-row">
                <button id="install-device" type="button" disabled>Install Poisoned_Os</button>
                <button id="cancel-install" type="button" class="quiet" disabled>Cancel transfer</button>
              </div>
            </div>
          </li>
        </ol>

        <div id="message" class="message" role="status" aria-live="polite">Select an official update package to begin.</div>
        <details class="technical-log"><summary>Technical log</summary><ol id="event-log"></ol></details>
      </section>
    </div>
    <footer><span>Runs entirely in this browser tab</span><span><a href="https://poisoned-os-site-production.up.railway.app/docs/">Read the docs ↗</a></span><span>Chrome / Edge / Chromium · HTTPS or localhost</span></footer>
  </main>
`;

function element<T extends HTMLElement>(selector: string): T {
  const selected = document.querySelector<T>(selector);
  if (!selected) throw new Error(`Installer element is missing: ${selector}`);
  return selected;
}

const packageFile = element<HTMLInputElement>("#package-file");
const releasePanel = element<HTMLDivElement>("#release-panel");
const releaseSelect = element<HTMLSelectElement>("#release-select");
const downloadButton = element<HTMLButtonElement>("#download-release");
const connectButton = element<HTMLButtonElement>("#connect-device");
const installButton = element<HTMLButtonElement>("#install-device");
const cancelButton = element<HTMLButtonElement>("#cancel-install");
const confirmation = element<HTMLInputElement>("#install-confirmation");
const message = element<HTMLDivElement>("#message");
const progressBar = element<HTMLProgressElement>("#install-progress");
const progressDetail = element<HTMLParagraphElement>("#progress-detail");
const statusChip = element<HTMLOutputElement>("#overall-status");
const deviceScreen = element<HTMLSpanElement>("#device-screen");
const cableProgress = element<HTMLSpanElement>("#cable-progress");
const eventLog = element<HTMLOListElement>("#event-log");

const installer = new BrowserFirmwareInstaller();
let bundle: UpdateBundle | null = null;
let releases: readonly InstallerRelease[] = [];
let busy = false;
let abortController: AbortController | null = null;

function text(selector: string, value: string): void {
  element<HTMLElement>(selector).textContent = value;
}

function log(value: string): void {
  const item = document.createElement("li");
  const time = document.createElement("time");
  time.dateTime = new Date().toISOString();
  time.textContent = new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  const content = document.createElement("span");
  content.textContent = value;
  item.append(time, content);
  eventLog.append(item);
}

function fail(reason: unknown): void {
  const value = reason instanceof Error ? reason.message : String(reason);
  message.textContent = value;
  message.dataset.kind = "error";
  statusChip.textContent = "Action needed";
  statusChip.dataset.kind = "error";
  log(`ERROR · ${value}`);
}

function setBusy(value: boolean): void {
  busy = value;
  packageFile.disabled = value;
  releaseSelect.disabled = value;
  downloadButton.disabled = value;
  connectButton.disabled = value;
  confirmation.disabled = value || !bundle || !installer.connectedIdentity;
  cancelButton.disabled = !value;
  updateReadiness();
}

function updateReadiness(): void {
  const ready = Boolean(bundle && installer.connectedIdentity);
  confirmation.disabled = busy || !ready;
  installButton.disabled = busy || !ready || !confirmation.checked;
  text("#review-state", ready ? "Ready" : "Blocked");
  text("#review-copy", ready && bundle && installer.connectedIdentity
    ? `${installer.connectedIdentity.hardwareModel} target ${installer.connectedIdentity.hardwareTarget} will receive ${bundle.versionLabel}. Archive ${bundle.archiveSha256.slice(0, 16)}…`
    : "Select a package and connect a verified Flipper Zero target 7.");
  document.querySelector('[data-stage="connect"]')?.classList.toggle("active", Boolean(bundle) && !installer.connectedIdentity);
  document.querySelector('[data-stage="review"]')?.classList.toggle("active", ready);
}

function onProgress(progress: InstallProgress): void {
  statusChip.textContent = progress.title;
  statusChip.dataset.kind = progress.phase === "complete" ? "success" : "working";
  progressDetail.textContent = progress.detail;
  progressBar.max = Math.max(progress.total, 1);
  progressBar.value = Math.min(progress.completed, progressBar.max);
  cableProgress.style.width = `${Math.min(100, Math.max(4, (progress.completed / Math.max(progress.total, 1)) * 100))}%`;
  text("#install-state", progress.phase === "complete" ? "Verified" : progress.phase.replaceAll("-", " "));
  deviceScreen.textContent = progress.phase === "complete" ? "POISONED_OS" : progress.phase.toUpperCase().slice(0, 12);
  if (["connecting", "checking", "preparing", "rebooting", "complete"].includes(progress.phase)) log(`${progress.phase.toUpperCase()} · ${progress.detail}`);
}

async function acceptArchive(bytes: Uint8Array, source: string, expectedDigest?: string): Promise<void> {
  message.dataset.kind = "working";
  message.textContent = "Opening and checking every package entry…";
  statusChip.textContent = "Checking package";
  const parsed = await extractUpdateBundle(bytes);
  if (expectedDigest && parsed.archiveSha256 !== expectedDigest) throw new Error("Selected release digest changed after download verification");
  bundle = parsed;
  confirmation.checked = false;
  text("#fact-package", parsed.versionLabel);
  text("#fact-digest", parsed.archiveSha256);
  text("#package-state", expectedDigest ? "Signature + package verified" : "Package checked");
  message.dataset.kind = "success";
  message.textContent = expectedDigest
    ? `${source} matches its signed target-7 release with ${parsed.files.length} files (${formatBytes(parsed.totalBytes)} expanded).`
    : `${source} is a structurally valid target-7 package with ${parsed.files.length} files (${formatBytes(parsed.totalBytes)} expanded). Local selection does not authenticate its publisher.`;
  statusChip.textContent = expectedDigest ? "Signed package ready" : "Local package ready";
  statusChip.dataset.kind = "success";
  log(`PACKAGE · ${source} · sha256 ${parsed.archiveSha256}`);
  updateReadiness();
}

packageFile.addEventListener("change", () => {
  const file = packageFile.files?.[0];
  if (!file) return;
  void file.arrayBuffer()
    .then((bytes) => acceptArchive(new Uint8Array(bytes), file.name))
    .catch(fail);
});

downloadButton.addEventListener("click", () => {
  const release = releases[releaseSelect.selectedIndex];
  if (!release) return;
  setBusy(true);
  message.dataset.kind = "working";
  message.textContent = `Downloading signed ${release.manifest.channel} ${release.manifest.version}…`;
  void downloadRelease(release, (received, total) => {
    progressBar.max = total;
    progressBar.value = received;
    progressDetail.textContent = `Downloaded ${formatBytes(received)} of ${formatBytes(total)}`;
  }).then((bytes) => acceptArchive(bytes, `${release.manifest.channel} ${release.manifest.version}`, release.component.sha256))
    .catch(fail)
    .finally(() => setBusy(false));
});

connectButton.addEventListener("click", () => {
  setBusy(true);
  void installer.connect(onProgress)
    .then((identity) => {
      text("#fact-device", `${identity.hardwareModel} · target ${identity.hardwareTarget}`);
      text("#fact-firmware", `${identity.firmwareOrigin || "Flipper firmware"} ${identity.firmwareVersion || "unknown"}`);
      text("#connect-state", "Verified");
      deviceScreen.textContent = "LINK READY";
      message.dataset.kind = "success";
      message.textContent = "The selected USB device passed the Flipper target and RPC identity checks.";
      log(`DEVICE · ${identity.hardwareModel} target ${identity.hardwareTarget} · ${identity.firmwareOrigin || "stock/other"} ${identity.firmwareVersion}`);
    })
    .catch(fail)
    .finally(() => setBusy(false));
});

confirmation.addEventListener("change", updateReadiness);

installButton.addEventListener("click", () => {
  if (!bundle || !confirmation.checked) return;
  setBusy(true);
  abortController = new AbortController();
  message.dataset.kind = "working";
  message.textContent = "Installation is running locally. Keep the cable connected.";
  log(`INSTALL · starting ${bundle.versionLabel}`);
  void installer.install(bundle, onProgress, abortController.signal)
    .then((result) => {
      message.dataset.kind = "success";
      message.textContent = `Installed and verified Poisoned_Os ${result.after.firmwareVersion}. The device is ready.`;
      text("#fact-firmware", `${result.after.firmwareOrigin} ${result.after.firmwareVersion}`);
      confirmation.checked = false;
      log(`COMPLETE · ${result.after.firmwareOrigin} ${result.after.firmwareVersion} · ${result.remoteManifest}`);
    })
    .catch(fail)
    .finally(() => {
      abortController = null;
      setBusy(false);
    });
});

cancelButton.addEventListener("click", () => {
  abortController?.abort();
  cancelButton.disabled = true;
  log("CANCEL · operator requested transfer cancellation");
});

function formatBytes(value: number): string {
  if (value < 1024) return `${value} B`;
  if (value < 1024 * 1024) return `${(value / 1024).toFixed(1)} KiB`;
  return `${(value / (1024 * 1024)).toFixed(1)} MiB`;
}

async function loadPublishedReleases(): Promise<void> {
  const configuredUrl = import.meta.env.VITE_POISON_RELEASE_FEED_URL as string | undefined;
  const keys = configuredTrustedKeys();
  const runtimeConfig = configuredUrl && Object.keys(keys).length > 0 ? null : await loadInstallerRuntimeConfig();
  const releaseFeedUrl = configuredUrl || runtimeConfig?.releaseFeedUrl;
  const trustedKeys = Object.keys(keys).length > 0 ? keys : runtimeConfig?.trustedReleaseKeys || {};
  if (!releaseFeedUrl || Object.keys(trustedKeys).length === 0) {
    log("RELEASES · no signed feed configured; local package installation remains available");
    return;
  }
  const feed = await loadReleaseFeed(releaseFeedUrl, trustedKeys);
  releases = [...feed.releases].sort((left, right) => {
    const order = { stable: 0, beta: 1, developer: 2, internal: 3 } as const;
    return order[left.manifest.channel] - order[right.manifest.channel] || right.manifest.version.localeCompare(left.manifest.version, undefined, { numeric: true });
  });
  releaseSelect.replaceChildren(...releases.map((release) => {
    const option = document.createElement("option");
    option.textContent = `${release.manifest.channel} · ${release.manifest.version} · ${formatBytes(release.component.bytes)}`;
    return option;
  }));
  releasePanel.hidden = false;
  log(`RELEASES · verified ${releases.length} signed published release${releases.length === 1 ? "" : "s"}`);
}

if (!window.isSecureContext || !serialApi()) {
  connectButton.disabled = true;
  fail(!window.isSecureContext
    ? "Open the installer over HTTPS or localhost before connecting a device."
    : "Web Serial is unavailable. Open this page in current Chrome, Edge, or another Chromium browser.");
} else {
  void loadPublishedReleases().catch((error) => {
    fail(error);
    log("RELEASES · published feed unavailable; a local official .tgz can still be selected");
  });
}

window.addEventListener("beforeunload", () => {
  void installer.disconnect();
});

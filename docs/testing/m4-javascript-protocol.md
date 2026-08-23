# M4 JavaScript Physical Wi-Fi Workflow Protocol

Poisoned_Os provides a JavaScript workspace whose source, immutable dependencies, runtime shims, artifact bytes, and lifecycle commands travel through the authenticated browser-to-device RPC session. The physical workflow uses the production browser dashboard, local Node.js runtime, supported Wi-Fi board, raw TCP/UART expansion RPC path, managed mJS runtime, and microSD-backed project storage; it does not substitute a mock transport or simulated device.

## Prerequisites

- Commission `tools/hil/inventory.json` as described in `tools/hil/README.md`.
- Install a production PoisonedOS build containing the secure-session, file-transfer, workload, and JavaScript runtime handlers. This protocol does not flash firmware.
- Attach the supported Wi-Fi board to the inventory `test` Flipper and keep that exact Flipper available for inventory verification.
- Install Chromium for the pinned Playwright version.
- Choose an explicit board ID and provide its reachable URL. Playwright starts the Node.js runtime on its exact loopback test origin:

```bash
POISON_HIL_WIFI_BOARD_ID=field \
POISON_WIFI_BOARDS='{"field":"http://blackmagic.local"}' \
  python3 tools/hil/run_suite.py --suite javascript-workflow --timeout 120
```

## Pass Contract

The suite resolves the test Flipper by the exact serial number in the private inventory and passes its resolved port to the dashboard. The browser must then complete all of these operations against that exact device:

1. Load the runtime manifest over HTTP, establish the authenticated web RPC socket through the selected Wi-Fi board, and complete browser plus physical-device confirmation.
2. Import the committed `tiny-value` source package only after its lock, file digest, package digest, runtime, license, and inventory validate.
3. Save the edited project as an IndexedDB revision.
4. Upload the immutable project manifest and members, create the managed workload, execute it on-device, and receive `dependency=7` through ordered workload console RPC frames.
5. Upload `report.json` beneath the executed project revision and finalize it through the workload artifact RPC.
6. Disconnect the authenticated session, pair again, restore the saved revision, and rerun it successfully.
7. Run a periodic Node-compatible timer on-device, receive a real timer callback through the workload console, issue Stop, and receive the terminal `cancelled` state.

An unspecified board ID/address, inventory mismatch, skipped physical input, rejected RPC receipt, incorrect console frame, failed artifact digest, or failure to restore the revision fails the suite. A successful dashboard unit/integration test run alone is not physical evidence.

## Served Interface Sandbox

The separately tagged `dashboard/e2e/javascript-sandbox.spec.ts` test validates a signed, already-installed `ui-pack` through the same physical Wi-Fi route and an opaque-origin sandbox. It requires the selected board plus the installed bundle ID, version, and content digest:

```bash
POISON_HIL_WIFI_BOARD_ID=field \
POISON_WIFI_BOARDS='{"field":"http://blackmagic.local"}' \
POISON_HIL_UI_BUNDLE_ID=<installed-package-id> \
POISON_HIL_UI_BUNDLE_VERSION=<installed-version> \
POISON_HIL_UI_BUNDLE_SHA256=<64-lowercase-hex-content-digest> \
  pnpm --dir dashboard e2e --grep "physical JavaScript sandbox"
```

That command proves bundle retrieval and browser isolation; it does not install the package. Wi-Fi loss, device reboot, capability revocation, and package installation/reinstallation remain separate physical cases until the HIL runner drives each real operation. They must not be reported as covered by the `javascript-workflow` suite. Runtime termination is part of the JavaScript workflow pass contract but remains unexecuted evidence until the physical suite itself passes.

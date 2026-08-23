# ADR-0002: Supported Client Transports

- **Status:** Accepted for V1 support policy
- **Date:** 2026-08-21
- **Owners:** Dashboard, Node Runtime, and Firmware

## Decision

Poisoned_Os has one dashboard route:

```text
browser -> HTTP(S) + WS(S) -> local Node.js runtime -> Wi-Fi -> Blackmagic board -> UART expansion RPC -> Flipper
```

Every segment is required. USB, Web Serial, Web Bluetooth, and the Rust bridge are not dashboard transports or compatibility fallbacks.

Poisoned_Os also has a separate installation route:

```text
Chromium browser -> Web Serial -> USB CDC RPC -> stock, third-party, or PoisonedOS Flipper -> on-device updater
```

This route belongs only to `@poisonedos/web-installer`. It installs the first Poisoned_Os image or performs a later cable update and then verifies the returned firmware identity. It does not expose dashboard controls, substitute for the Wi-Fi board, or change the dashboard support matrix. Web Serial limits this installer route to secure contexts in compatible Chromium browsers; mobile and non-Web-Serial browsers may still use the dashboard after installation but cannot use this cable installer.

The browser-facing transport is `web`. The Node.js runtime serves the application and discovery manifest over HTTP or HTTPS and carries opaque encrypted RPC bytes over an authenticated WebSocket or secure WebSocket. The manifest declares both protocol pairs and identifies Wi-Fi as the required board network. Browser clients derive the WS/WSS endpoint from the page origin, enforce same-origin discovery, and reject a runtime that does not declare `web` as its primary route.

The runtime terminates HTTPS itself when certificate and key paths are configured. A browser on another device uses HTTPS/WSS because the pairing and session cryptography requires a secure browser context. HTTP/WS remains valid for loopback development and same-host use.

The board-facing transport is Wi-Fi. The runtime configures the pinned official Blackmagic firmware through its HTTP UART API, owns its single raw TCP-to-UART connection on port `3456`, performs the Flipper expansion heartbeat and baud negotiation, starts RPC, keeps the expansion session alive, applies framing and backpressure, and stops RPC before closing. Because the board accepts one raw TCP client, the Node.js runtime is the sole owner and allows only one active browser RPC session per board.

Other local Node.js processes receive independent named loopback addresses through the runtime registry. The browser never receives those loopback addresses. The runtime publishes authenticated same-origin HTTP(S) and WS(S) proxy paths for each process, preserves the request path and query, rejects cross-origin or unauthenticated access, and establishes an upstream process WebSocket before accepting the browser upgrade so the first message cannot be lost.

The V1 matrix lists exact operating-system/browser floors and records whether a row is supported or still planned. Every row uses only `web-runtime`. A row can become `supported` only after a physical host and device identifier, the web-runtime route, and a named test command are recorded. Rows without that evidence remain `planned` and cannot be advertised by release tooling.

The session contract separates an ephemeral P-256 ECDH key from the browser's stable, non-extractable P-256 signing identity. First pairing signs the displayed transcript and requires device approval. Firmware transactionally persists an enclave-authenticated identity digest, name, and role in alternating, generation-numbered slots; load verifies both slots and selects the newest valid generation, so an interrupted write leaves the prior committed state available. A returning client proves possession by signing a new transcript. Device Security lists those records and can revoke one or all. Encrypted dispatch and resume both recheck the registry, so revocation denies the active client on its next request and rejects stored resume tokens. A corrupt or device-key-invalid registry fails pairing closed and exposes only an explicit on-device reset operation that replaces the invalid records and appends a recovery audit event.

## Distribution policy

- macOS, Windows, Linux, Android, and iOS rows are maintained independently; support claims are not inferred from similar platforms.
- Dashboard browser support depends on standards-based HTTP(S), WS(S), and the required secure-browser cryptography, not direct USB or Bluetooth APIs.
- Cable-installer support is independently limited to browsers exposing Web Serial and is never inferred from a dashboard browser row.
- Wi-Fi board reachability and a real browser-to-runtime-to-board-to-device workflow are mandatory parts of physical support evidence.

## Evidence

- Machine-readable matrix: `config/supported-clients.json`
- Verifier: `tools/verify_supported_clients.py`
- Regression tests: `tools/tests/test_supported_clients.py`
- Browser web-transport tests: `dashboard/src/transports/WifiGatewayTransport.test.ts`
- Node runtime integration tests: `dashboard/server/runtime-server.test.ts`
- Board connection tests: `dashboard/server/wifi-board-connection.test.ts`
- Board firmware provenance: `provenance/blackmagic.lock.json`
- Cable installer verification: `web-installer/src/*.test.ts`

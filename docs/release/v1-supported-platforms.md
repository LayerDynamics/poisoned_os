# Poisoned_Os V1 Supported Platforms

This release record is maintained from ADR-0002 and the M0–M5 evidence ledger. Every dashboard row uses the local Node.js `web-runtime` route: HTTP(S)/WS(S) from browser to runtime and mandatory Wi-Fi/TCP/UART from runtime to the physical Flipper. USB, Bluetooth, and the Rust bridge are not dashboard routes.

Current platform rows remain **pending release evidence**. A row is not supported merely because its browser and runtime code compile; it requires a produced artifact, HTTPS trust/install evidence where the browser is remote, and a complete physical browser → Node runtime → Wi-Fi board → Flipper workflow. The machine-readable matrix is [`config/supported-clients.json`](../../config/supported-clients.json).

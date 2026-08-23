# Self-hosted Dashboard

**Owner:** Dashboard and Node runtime maintainers

**Prerequisites:** The packaged `@poisonedos/dashboard` Node.js runtime, a supported Wi-Fi board reachable from the runtime host, and a trusted certificate/key when browsers connect from another device.

**Procedure:** Configure `POISON_WIFI_BOARDS`, start the Node.js runtime, open its HTTP loopback address or trusted HTTPS LAN address, select the named board, pair the device, and perform the supported control and evidence workflows.

**Verification:** Confirm the manifest declares `web` over HTTP(S)/WS(S), browser traffic remains on the runtime origin, the runtime owns one TCP/UART session per selected board, `pnpm --dir dashboard verify` passes, and the package plus Blackmagic provenance digests are retained.

**Escalation:** Stop the runtime and quarantine the package if browser requests leave the configured origin, a non-Wi-Fi dashboard route appears, or the runtime reports an unexpected board owner.

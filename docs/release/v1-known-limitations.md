# Poisoned_Os V1 Known Limitations

- Wasm execution is fail-closed until ADR-0006 records a measured runtime decision.
- The local Node.js runtime, HTTP(S)/WS(S) transport, same-origin Node-service proxies, Wi-Fi board owner, and expansion framing have automated coverage, but the complete physical browser → runtime → Wi-Fi board → Flipper E2E workflow still requires release evidence.
- Remote phone use requires an HTTPS certificate trusted by that phone and valid for the runtime's LAN hostname or address; certificate setup and installation UX are not packaged yet.
- Physical recovery, update rollback, accessibility-study, and classroom E2E evidence remain required for stable release.
- The standalone Web Serial installer has archive, RPC, reconnect, and orchestration regression coverage, but a physical first-install and interrupted-update evidence run is still required. Its Railway developer deployment can serve repository-built signed objects, but GitHub Pages/stable publication remains disabled until the production release key is commissioned and both recorded distribution-license blockers are resolved.
- The local builder foundation is implemented, but independent sandbox/restart evidence remains open.

These limitations are release-blocking evidence gaps, not claims of completed V1 support.

# ADR-0001: Distribution and Hosted-Service Boundary

- **Status:** Accepted for V1
- **Date:** 2026-08-21
- **Owners:** Product and Release

## Decision

Poisoned_Os ships with three explicit distribution profiles: `local-only`, `self-hosted`, and `hosted`. Device control, files, evidence export, customization, and already-installed workloads are local capabilities in every profile. Dashboard control uses the local Node.js runtime and Wi-Fi board but does not require a WAN connection or hosted service.

The `local-only` profile disables catalog, hosted build, synchronization, and organization features and rejects external requests from core components. The `self-hosted` profile may connect only to operator-configured endpoints. The `hosted` profile may use the managed endpoints listed by its signed configuration. A profile cannot silently promote itself to another profile.

The V1 product ships the local-only profile. Self-hosted and hosted behavior is represented in the same machine-checked matrix so future builds cannot accidentally make local workflows account-dependent. Hosted catalog, synchronization, organization management, and hosted builds are post-V1 unless separately approved with source, license, retention, and offline behavior evidence.

The standalone cable installer is a static browser tool, not the dashboard or a hosted control service. Local package selection performs no network request. A commissioned Pages build may fetch only its configured release feed and immutable firmware archive; the browser admits a published archive only after verifying the embedded release trust key, signed manifest, selected-component digest, and exact byte count. The workflow cannot publish unless release distribution is explicitly approved.

## Consequences

- On-device operation remains usable without networking; dashboard operation always requires the local Node.js runtime and Wi-Fi board path.
- Every optional service has an explicit owner, data boundary, and profile state.
- Adding a hosted dependency requires a matrix change and verifier coverage.
- The dashboard and Node.js runtime must use the matrix rather than importing a hosted client directly.
- Hosting the static installer does not move device control, USB RPC, archive parsing, or firmware bytes through a PoisonedOS service; those operations stay in the browser and on the cable.

## Evidence

- Machine-readable policy: `config/features/local-only.json`
- Verifier: `tools/verify_feature_matrix.py`
- Regression tests: `tools/tests/test_feature_matrix.py`

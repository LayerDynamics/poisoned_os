# Local-only Operation

**Owner:** Release and Node runtime maintainers

**Prerequisites:** A verified local dashboard/Node runtime bundle, supported Wi-Fi board, paired device, and a local network isolated from WAN routes.

**Procedure:** Set `POISON_PROFILE=local-only`, keep the local Wi-Fi path available, start the Node runtime with the explicit board address, pair the device, and exercise files, evidence, installed workloads, profiles, lessons, and recovery while WAN access is blocked at the test-network boundary.

**Verification:** Run `python3 tools/hil/run_suite.py --suite local-only` and confirm the HIL evidence records `localNetworkRequiredForDashboard: true` and `externalServiceRequired: false`.

**Escalation:** Stop promotion if any external request is attempted or a local workflow requires hosted credentials; preserve the command output digest and candidate manifest.

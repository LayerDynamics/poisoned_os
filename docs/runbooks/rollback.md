# Rollback Runbook

## Owner

Firmware release maintainer; backup is the recovery-device operator.

## Prerequisites

Keep the previous signed manifest, firmware bundle, recovery device, and user-data-preserving recovery procedure available.

## Procedure

```bash
python3 tools/release/verify_release.py artifacts/release-evidence/rollback.json --root .
./fbt firmware_all updater_all resources
```

Use the existing updater state machine and select the recorded rollback target; never delete user data as part of rollback.

## Verification

Confirm the previous version boots, the update state is terminal, user data remains present, and the rollback artifact digest matches its manifest.

## Escalation

If activation or recovery is incomplete, stop repeated attempts and use the dedicated recovery-device procedure.

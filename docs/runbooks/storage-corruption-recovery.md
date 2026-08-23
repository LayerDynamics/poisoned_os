# Storage Corruption Recovery Runbook

## Owner

Storage maintainer; backup is the recovery-device operator.

## Prerequisites

Preserve the original media, record its digest and device ID, and ensure user-data preservation is selected.

## Procedure

```bash
./fbt FIRMWARE_APP_SET=unit_tests
python3 tools/hil/run_suite.py --suite device-recovery
```

Stop writes, run the dashboard-independent reindex/recovery operation, and restore only from a verified last-known-good source.

## Verification

Confirm the original bytes were preserved, unsafe ancestry is rejected, indexes rebuild deterministically, and user data remains accessible.

## Escalation

Escalate tampering, broken audit ancestry, or failed reindex to the evidence and storage maintainers.

# Device Recovery Runbook

## Owner

Hardware-in-loop maintainer; backup is the firmware maintainer.

## Prerequisites

Use the recovery-role device, dedicated recovery transport, known SD fixture, controllable USB power, and a verified last-known-good artifact.

## Procedure

```bash
python3 tools/hil/run_suite.py --suite device-recovery
```

Preserve user data, enter recovery from the device, cancel safely when requested, and restore firmware/profile/index through the recovery state machine.

## Verification

Confirm the device reboots, recovery reaches a terminal state, the fixture digest is unchanged, and normal firmware launches afterward.

## Escalation

Stop if the device does not respond or user-data preservation cannot be proven; use the dedicated SWD recovery path.

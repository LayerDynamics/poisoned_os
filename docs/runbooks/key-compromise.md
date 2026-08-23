# Key Compromise Runbook

## Owner

Security maintainer; backup is the release maintainer.

## Prerequisites

Identify the affected key ID, preserve signed audit evidence, and obtain the next approved key through the key ceremony.

## Procedure

```bash
python3 tools/signing/verify_manifest.py --help
python3 tools/release/verify_release.py artifacts/release-evidence/revocation.json --root .
```

Revoke the compromised key or artifact, issue a replacement signed manifest, and halt affected promotion channels.

## Verification

Verify old signatures are denied, the replacement key is accepted, and all affected packages or releases are rechecked.

## Escalation

Escalate suspected private-key exposure immediately; do not continue normal rollout while revocation evidence is incomplete.

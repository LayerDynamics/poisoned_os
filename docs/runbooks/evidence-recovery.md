# Evidence Recovery Runbook

## Owner

Evidence maintainer; backup is the support maintainer.

## Prerequisites

Retain the case ID, content and audit digests, export manifest, and original source bytes. Do not repair corrupted records in place.

## Procedure

```bash
python3 tools/rust/cargo.py test --manifest-path bridge/Cargo.toml
pnpm --dir dashboard verify
```

Verify the immutable record chain, rebuild derived indexes, and import only records whose content and audit links verify.

## Verification

Confirm repeated exports are byte-identical, malformed ancestry is rejected, and no record is marked complete before commit acknowledgement.

## Escalation

Escalate any digest mismatch, silent repair, or acknowledged-data loss to the evidence and security maintainers.

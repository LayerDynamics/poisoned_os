# Security Incident Response

1. Stop promotion and record the candidate, manifest digest, finding ID, severity, owner, and UTC time.
2. Revoke the affected key or artifact digest through the signed revocation process.
3. Preserve local logs and digests without exporting secrets or raw payloads.
4. Add a regression test to the owning subsystem before reopening promotion.
5. Re-run the adversarial matrix, release verifier, and affected milestone gates.

Critical and high findings cannot be suppressed for a stable candidate.

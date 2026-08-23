# Privacy-Preserving Diagnostics

Diagnostics contain bounded counters, redacted summaries, versions, correlation digests, and digest-only `/ext/` file references. They do not contain secrets, private keys, credentials, raw payloads, source files, evidence contents, or stable personal identifiers.

Validate exported fixtures with:

```bash
python3 tools/security/verify_redaction.py path/to/support-bundle.json
```

Support export requires previewed consent and remains local unless the operator explicitly chooses an approved export path.

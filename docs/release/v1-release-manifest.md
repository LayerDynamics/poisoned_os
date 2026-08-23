# V1 Release Manifest Contract

`schemas/poison/release-manifest.schema.json` defines the release-level composition of firmware, resources, dashboard, bridge, builder/SDK, schemas, licenses, SBOM, provenance, and rollback components. Verify an assembled manifest with:

```bash
uv run --no-project --python 3.11 python tools/release/verify_release.py path/to/release.json --root path/to/release-root
```

Verification is digest- and path-based and fails closed on missing, changed, duplicated, or escaping components. It also validates version bounds, unique lowercase revocation digests, and optional ECDSA-P256-SHA256 signature metadata/encoding. The general `verify_release.py` check does not cryptographically verify that optional signature; a signing verifier must do so with the release public key.

`tools/release/build_web_installer_feed.py` is the narrower publication gate for `@poisonedos/web-installer`. It requires the signature, verifies it cryptographically with the configured P-256 public key, and admits only a `.tgz` component whose exact bytes and SHA-256 match the signed manifest. This publication check does not replace the on-device updater, rollback policy, signing authority, or full release gate.

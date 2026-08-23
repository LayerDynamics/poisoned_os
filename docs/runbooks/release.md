# Release Runbook

## Owner

Release maintainer; backup is the firmware maintainer.

## Prerequisites

Use a reviewed candidate, current static-gate results, release manifest, signatures, and evidence ledger. Do not promote without explicit approval.

Public distribution is prohibited until the flipperzero-protobuf license grant and mJS GPL compatibility findings recorded in the signing policy are resolved. Browser-installer publication additionally requires a commissioned firmware release key, completed key ceremony, working revocation path, and a protected `github-pages` environment.

## Procedure

```bash
uv run --no-project --python 3.11 python tools/release/run_static_gates.py --profile stable --results artifacts/release-evidence/static-gates.json
uv run --no-project --python 3.11 python tools/release/verify_release.py artifacts/release-evidence/release.json --root .
```

Promote one channel at a time and retain the exact manifest and output digests.

## Browser Installer Publication

New-version artifact generation is handled by the dedicated Railway `poisoned-artifacts` service. Set its release variables and call its authenticated `POST /v1/build` endpoint with the new version and channel; the service runs `updater_package`, signs the exact archive, and replaces the served feed/config/package atomically. The service also builds automatically on startup when `POISON_RELEASE_VERSION` is configured. Its private signing key never appears in served artifacts.

The published GitHub release must contain these exact asset names:

- `release.json`, signed by the commissioned firmware release key;
- `flipper-z-f7-update-poisonedos.tgz`, represented in that manifest at `dist/f7-C/flipper-z-f7-update-poisonedos.tgz` with its exact size and SHA-256.

The release tag must equal the signed manifest version, with an optional `v` prefix; for example, signed version `1.2.3` may use tag `1.2.3` or `v1.2.3`.

Before enabling distribution, configure these GitHub repository variables:

- `POISON_RELEASE_KEY_ID`: the signed manifest's commissioned key ID;
- `POISON_RELEASE_PUBLIC_KEY_PEM`: the corresponding SPKI public key PEM;
- `POISON_RELEASE_KEYS_JSON`: a JSON object containing exactly the same key ID and PEM, for example `{"firmware-release-1":"-----BEGIN PUBLIC KEY-----\n...\n-----END PUBLIC KEY-----"}`;
- `POISON_RELEASE_DISTRIBUTION_APPROVED`: leave unset or `false` while any release blocker remains; set to `true` only after explicit Release and Security approval.

Configure Pages to use GitHub Actions as its source. Publishing a GitHub release then runs `.github/workflows/web-installer.yml`: it verifies the independent web module, downloads the two named assets, binds the release tag to the signed version, confirms the three key variables agree, cryptographically verifies the manifest and package, builds the static site with the public key embedded, uploads the immutable package beside it, and deploys through the protected `github-pages` environment. A failed or skipped gate does not deploy.

For a pre-publication check using a non-production fixture key, run:

```bash
uv run --no-project --python 3.11 python tools/release/build_web_installer_feed.py \
  --root path/to/release-root \
  --output path/to/releases.json \
  --public-key firmware-test-1=path/to/public.pem \
  --release path/to/release.json path/to/release-root/dist/f7-C/flipper-z-f7-update-poisonedos.tgz releases/v1.2.3/flipper-z-f7-update-poisonedos.tgz
pnpm --dir web-installer install --frozen-lockfile
pnpm --dir web-installer verify
```

## Verification

Confirm all commands exit zero, the release manifest points to existing digest-matching components, the evidence ledger has no failed entries, the workflow feed builder reports no signature or package mismatch, and the deployed installer is exercised on the physical supported-browser/device row before promotion completes.

## Escalation

Stop promotion and page the release maintainer for any signature, digest, rollback, or evidence failure.

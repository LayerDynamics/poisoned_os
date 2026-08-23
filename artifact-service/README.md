# Poisoned_Os artifact service

This Railway service owns new-version artifact generation. It builds the target-7 updater package, creates the signed manifest and feed, writes the installer runtime trust configuration, and serves the resulting immutable package.

Configure `POISON_RELEASE_PRIVATE_KEY_B64`, `POISON_RELEASE_KEY_ID`, and `ARTIFACT_BUILD_TOKEN` as Railway secrets. Set `POISON_RELEASE_VERSION` and `POISON_RELEASE_CHANNEL` to build automatically when the service starts. To build a later version without replacing the service, call:

```bash
curl -X POST https://<artifact-service-domain>/v1/build \
  -H 'Authorization: Bearer <ARTIFACT_BUILD_TOKEN>' \
  -H 'Content-Type: application/json' \
  --data '{"version":"1.2.3","channel":"developer"}'
```

Poll `/healthz`; once it reports `ready`, the service serves `releases.json`, `release.json`, `installer-config.json`, and the versioned `.tgz` under `/releases/<version>/`.

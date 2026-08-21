# PoisonedOS Provenance

PoisonedOS is a derivative firmware distribution built from the official
`flipperdevices/flipperzero-firmware` development snapshot at commit
`a55e39395ff31bd5fdf3929c70720a7fb76e5968`. The read-only checkout under
`do_not_include/flipperzero-firmware` is the authoritative comparison source;
it is not part of the product tree and must never be flattened into product
history.

## Baseline Lock

`baseline.lock.json` uses schema `poison.baseline/v1`. Its `files` array records
the normalized relative path, source classification, Git-compatible mode, and
SHA-256 digest for every materialized product file. Classifications mean:

- `upstream`: path, mode, and bytes match the locked official snapshot.
- `dependency`: path is materialized under one of the twelve locked upstream
  dependency gitlinks.
- `poison-modified`: the official snapshot owns the path, but PoisonedOS has
  intentionally different bytes or mode.
- `poison-added`: the path does not exist in the official snapshot.

`upstreamTreeSha256` is the SHA-256 of the UTF-8 concatenation of all sorted
`path NUL mode NUL sha256 LF` records. Despite the field's schema-preserved
name, the value commits to all four classifications, including PoisonedOS-only
files. The verifier rejects malformed metadata, changed bytes or modes, missing
paths, untracked product paths, and incomplete dependency pins.

The lock intentionally excludes comparison clones, Git metadata, generated
build output, dependency caches, local hardware-in-loop inventory, Python byte
code, dashboard dependencies, and Rust target directories. These are either
reconstructable outputs, local secrets/device identities, or read-only evidence
sources rather than distributed product inputs.

Run the verifier from the workspace root:

```bash
python3 tools/verify_baseline.py
```

## Licensing State

The PoisonedOS-authored and upstream root firmware code is GPLv3-only; the
materialized tree also contains the separately licensed components inventoried
below. The workspace root contains the authoritative GPLv3 `LICENSE` and the
upstream build/contribution policy surface restored from the locked source. The
root `.gitattributes` and two dependency attribute files carry the ADR-0003 vendor
adaptation that prevents Git from changing locked dependency line endings;
all other restored Task 4 files remain byte-identical to OFW. Component-specific
license evidence is recorded in `components.json` and `licenses.json`, and the
deterministic SPDX/CycloneDX gate reports no unidentified license IDs and emits
explicit release-blocker metadata. Two component blockers remain:

- `flipperzero-protobuf` publishes no license grant at its pinned commit, so it
  is recorded as non-redistributable pending a rights-holder grant.
- mJS is GPL-2.0-only unless covered by a separate Cesanta commercial license,
  but the firmware build links it into the GPL-3.0-only program. Distribution is
  blocked until compatible rights are documented or mJS is replaced.

PoisonedOS is therefore not yet legally complete or distributable.

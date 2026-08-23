# Locked OFW Parity Evidence

PoisonedOS preserves the Flipper Zero F7 firmware platform while changing the product UI and adding explicitly governed capabilities. This record proves that those product deltas remain bounded against the locked Official Firmware baseline; it is a host build/parity gate, not a physical-device end-to-end test.

## Baseline and Hardware

- Recorded: 2026-08-21 in `America/Chicago`.
- Official Firmware commit: `a55e39395ff31bd5fdf3929c70720a7fb76e5968`.
- Host: `macOS-26.4-arm64-arm-64bit-Mach-O`, architecture `arm64`.
- Firmware target: Flipper Zero F7, target number `7`.
- Physical device used: no. The separate HIL suites own device recovery, firmware-unit execution, and USB identity evidence.

## Executed Command Matrix

The following phases all exited `0` through `python3 tools/run_official_parity.py --baseline do_not_include/flipperzero-firmware`. The unit-test configuration was followed by a production rebuild in each checkout, so both build trees ended in their production configuration.

| Checkout | Command | Result |
|---|---|---|
| PoisonedOS | `./fbt lint_all` | Pass |
| PoisonedOS | `./fbt firmware_all updater_all resources` | Pass |
| PoisonedOS | `./fbt FIRMWARE_APP_SET=unit_tests firmware_all` | Pass |
| PoisonedOS | `./fbt firmware_all updater_all resources` | Pass; production restored |
| Locked OFW | `./fbt lint_all` | Pass |
| Locked OFW | `./fbt firmware_all updater_all resources` | Pass |
| Locked OFW | `./fbt FIRMWARE_APP_SET=unit_tests firmware_all` | Pass |
| Locked OFW | `./fbt firmware_all updater_all resources` | Pass; production restored |

## Classified Compatibility Deltas

The generated F7 API moves from OFW `88.2` to PoisonedOS `88.3`. The only symbol-table addition is the supported function `void menu_set_header(Menu*, const char*)`; there are no removals, renumbers, signature changes, status changes, or other additions. `provenance/firmware-api.lock.csv` records all 3,849 current symbols with their GNU name hash, normalized signature, source owner, and supported/disabled classification.

All eight canonical `assets/protobuf/*.proto` sources are byte-identical to the locked OFW tree. The only accepted `fbt_options.py` differences are `FIRMWARE_ORIGIN`, from `Official` to `PoisonedOS`, and `DIST_SUFFIX`, from `local` to `poisonedos`; the parity checker normalizes those two assignments and requires the rest of the file to remain byte-identical.

## Artifact Evidence

Both firmware and updater metadata report target `7`. Differences in build date, commit, and branch are classified as build metadata. Binary, DFU, and ELF differences are classified as product-source deltas only after the locked source comparison has accounted for repository path differences; DFU and ELF magic and non-truncated binary payloads are validated before comparison.

| Artifact | OFW bytes | PoisonedOS bytes | OFW SHA-256 | PoisonedOS SHA-256 |
|---|---:|---:|---|---|
| Firmware BIN | 800456 | 801272 | `8e7bbdcccea99bf5bcbea0f336e3cf041a4e3b9639c1afbf00725a97ca3754a6` | `7b245bfd817a120807d9102ceaba5039c38d1fe6112d2f3651ee0884660a1482` |
| Firmware DFU | 800765 | 801581 | `eaf8ed54a31d8ddb7a7bd11f76ccefd54f5967cb9e1e030ebb72c1fe0be79013` | `67faacb8a408e855fef9f247f5ba77b88edbf55a5d244997433e98fd3158225a` |
| Firmware ELF | 9680760 | 9622884 | `9a32768b81868a5b36cd4225b860e5350ed23015a87c96151ae2a2cb1bdbc42a` | `2beacd21b7acdb0a83d7f7dc42282b764f3fb378f259208e2699b4a5d9e40266` |
| Updater BIN | 119769 | 119773 | `f4ba4388e222927ce98ab0353fb7bc269900e9dd62d9dfea186751c385d3914e` | `46983fa5cb385287c93c095da0cd4c677897b0afbbc3e1ec74938d2cdb4d4db1` |
| Updater DFU | 120078 | 120082 | `566c8048a4114dd9afe68beefb1967c9e59dda5ee35f7c16871496cf02ab800d` | `979a8221bf86babc84576980986f2c6e3e688fd123beed8229a425f0410316e3` |
| Updater ELF | 2186580 | 2168760 | `f4e0958b5ce34b3eb05b9fb3655fcb9af5c6a637b12c5440d1b74721dd816672` | `fb0e7b709ec9f2abd66f199344c7a4590112f8574a2a1e9a8162122ad6e4ab3b` |

The OFW metadata is build date `17-08-2026`, commit `a55e3939`, branch `dev`; the PoisonedOS metadata is build date `21-08-2026`, commit `40c50f55`, branch `main`. These are expected identity/build-context differences, not behavioral parity claims.

## Reproduction

Run the deterministic API check and then the parity matrix from the PoisonedOS repository root:

```bash
python3 tools/snapshot_firmware_api.py --check
python3 tools/run_official_parity.py --baseline do_not_include/flipperzero-firmware
```

The first command must print `firmware API snapshot verified`. The second must finish with `official parity passed`; any unexpected API, build-option, protobuf, artifact-format, target, or baseline-commit delta exits nonzero.

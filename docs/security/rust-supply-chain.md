# Rust supply-chain verification

`tools/rust/verify_vendor.py --locked` is the admission check for Rust inputs.
It reads `rust-sdk/Cargo.lock`, the checked-in `rust-sdk/vendor/` tree, and
`tools/rust/approved-crates.json`. The three views must describe the same
package set, source, checksum, license, feature set, and safety review.

The check is intentionally offline. It does not download crates, resolve a
new lockfile, trust a registry index, or accept a host Cargo configuration.
Run it before building the SDK, native FAP, or Wasm artifacts. A changed
lockfile or vendor file requires refreshed approval metadata and review.

Use `python3 tools/rust/cargo.py` for repository Cargo commands. It resolves
the installed numeric toolchain from `rust-toolchain.toml` and invokes its
Cargo binary directly, so an offline build does not trigger rustup metadata
refreshes.

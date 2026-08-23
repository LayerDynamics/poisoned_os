# V1 Fuzzing Matrix

The release fuzz matrix is local and bounded. It runs the four checked-in Rust target crates for protocol/session envelopes, package manifests, evidence manifests, and Wasm module admission through the pinned Cargo wrapper:

```bash
python3 tools/security/run_fuzz_matrix.py --profile release
```

Each target must compile and pass its regression corpus within the configured timeout. Missing targets, non-zero exits, and timeouts fail the matrix. Physical replay of minimized cases and sanitizer-duration evidence remain separate release gates.

# V1 Static Analysis Report

`config/release-static-gates.json` is the authoritative stable profile. Run:

```bash
python3 tools/release/run_static_gates.py --profile stable --results artifacts/release-evidence/static-gates.json
```

The runner records command, exit status, elapsed time, and output digest for each gate. An unavailable tool, timeout, nonzero command, or incomplete result is a release failure. The current report is not a stable-release pass until all M0–M5 and physical gates are complete.

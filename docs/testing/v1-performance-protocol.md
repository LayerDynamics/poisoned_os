# V1 Performance Protocol

The performance HIL suite requires a real test and recovery device inventory and a measured evidence file using `poison.performance/v1`. It probes both devices, then runs the same distribution comparator used by release tooling:

```bash
python3 tools/hil/run_suite.py --suite v1-performance --performance-evidence artifacts/release-evidence/performance.json
```

Evidence must contain the locked sample counts and p95/max/min budgets. Missing, malformed, or failing distributions fail the suite; averages cannot substitute for distributions. The suite records the evidence path and executed commands in the HIL result.

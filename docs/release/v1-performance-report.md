# V1 Performance Evidence Contract

Performance evidence is recorded as `artifacts/release-evidence/performance` and compared with:

```bash
python3 tools/release/compare_budgets.py artifacts/release-evidence/performance
```

The input must use `poison.performance/v1` and contain one measurement per required budget. Each measurement records finite non-negative samples and a minimum sample count; the comparator evaluates p95, maximum, and minimum bounds without replacing distributions with averages. No physical performance evidence is currently populated, so the M6 performance gate remains open.

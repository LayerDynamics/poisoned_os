# V1 Security Test Matrix

The machine-readable matrix in `docs/security/v1-security-test-matrix.json` names each critical boundary, its owning subsystem, severity, and executable check. Run the release-candidate matrix with:

```bash
python3 tools/security/run_adversarial_suite.py --release-candidate
```

The runner fails on missing owners, failed checks, timeouts, or open critical/high findings. This host matrix is evidence for the executable checks; physical peer, key-rotation, and penetration exercises remain separate gates.

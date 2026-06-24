# LDC host tests

These tests compile the production LDC core directly on the host. ThreadX, HAL,
and board code are intentionally excluded so the framing and queue behavior can
be verified independently.

Run all communication tests from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File tests/run_host_tests.ps1
```

`test_ldc` also prints a deterministic throughput baseline. The number is a
comparison aid for later changes, not a target-MCU timing measurement.

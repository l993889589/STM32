# LDC host tests

These tests compile the production LDC core directly on the host. ThreadX, HAL,
and board code are intentionally excluded so the framing and queue behavior can
be verified independently.

Run on Windows:

```powershell
cmake -S tests/ldc -B build/ldc-tests -G "MinGW Makefiles"
cmake --build build/ldc-tests
ctest --test-dir build/ldc-tests --output-on-failure
```

`test_ldc` also prints a deterministic throughput baseline. The number is a
comparison aid for later changes, not a target-MCU timing measurement.


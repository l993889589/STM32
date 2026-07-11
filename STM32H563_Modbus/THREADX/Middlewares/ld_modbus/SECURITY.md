# Security policy

`ld_modbus` parses untrusted serial and network frames, so bounds and length
validation defects are treated as security issues. Please report suspected
memory-safety problems privately to the repository owner before public detail.

There is currently no stable long-term-support branch. Fixes target the latest
release and are backported only when explicitly documented.

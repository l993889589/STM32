# Third-Party Notices

## micro-ecc

- Project: `micro-ecc`
- Upstream: <https://github.com/kmackay/micro-ecc>
- Imported revision: `541b3a78026420a3e369c4c9281c396b5e531113`
- Local path: `shared/third_party/micro-ecc`
- License: BSD 2-Clause; the complete text is retained in
  `shared/third_party/micro-ecc/LICENSE.txt`.

The STM32H563 Bootloader builds only the `secp256r1` verification path. Other
curves, compressed points and architecture-specific assembly are disabled by
the Keil target configuration. The library is used only to verify public OTA
signatures; no private signing key is present in the firmware repository.

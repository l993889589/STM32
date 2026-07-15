# Third-party software

## Eclipse ThreadX NetX Duo

- Upstream: https://github.com/eclipse-threadx/netxduo
- Version: `v6.1.7_rel`
- Commit: `c187d60014b149ee7c5e79b4d84c416caddbdef7`
- Imported paths: `NetXDuo/common`, `NetXDuo/ports/cortex_m7/ac5`, `NetXDuo/addons/dhcp/nxd_dhcp_client.*`
- License files: `NetXDuo/LICENSE.txt`, `NetXDuo/LICENSED-HARDWARE.txt`

The NetX Duo sources are kept unmodified. Project-specific configuration is
provided by `User/nx_user.h`; the STM32H750 Ethernet link driver remains in the
project BSP/application source tree.

## AP6212 WICED Wi-Fi binary

- Required local path: `Libraries/AP6212/libwifi_6212_armcm7_2.1.2_armcc.lib`
- Origin: the official RT-Thread ART-Pi support package / matching AP6212 WICED package

The prebuilt Wi-Fi library is proprietary vendor material and is intentionally
not committed to the parent repository. Copy the matching library to the path
above before building the `Flash` target. The surrounding board integration
source is published, but the repository does not grant redistribution rights
for the binary library.

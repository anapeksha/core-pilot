# Core Pilot Gateway ESP32-H2 RCP

This is the separately buildable OpenThread UART RCP shipped inside each signed CoreS3 application. It is pinned to ESP-IDF 6.0.2 and uses Spinel over UART0 at 460800 baud.

The CoreS3 side uses RX GPIO10, TX GPIO17, EN GPIO7, and BOOT GPIO18, matching Espressif's M5Stack Thread Border Router configuration. The M5Stack Module Gateway H2 v0.4 schematic routes H2 UART0 as RX GPIO23 and TX GPIO24; these module-specific values and its 2 MiB flash capacity are set in `sdkconfig.defaults`.

Build it independently from the repository root:

```sh
eim run 'idf.py -C firmware/h2 -B firmware/h2/build -D GATEWAY_RCP_MANIFEST_HEX=0000000000000000000000000000000000000000000000000000000000000000 build' v6.0.2
```

Normal CoreS3 builds call `tools/build_gateway_rcp.py`, derive a deterministic recipe digest, build this project, validate the expected sparse artifacts, and generate uncommitted embedding sources under the CoreS3 build directory. The signed CoreS3 OTA image therefore authenticates the exact RCP payload it may install. Before Thread starts on every CoreS3 boot, `core_gateway` enters the H2 ROM loader and validates each installed segment against its embedded SHA-256 digest; mismatches are reflashed and read back for SHA-256 verification.

This is recurring controller-side integrity validation, not H2 secure boot or H2 flash encryption. A physical attacker can still modify H2 flash while the system is running, until the next CoreS3 boot-time validation. Enabling H2 secure boot would require a separately managed signing/provisioning design compatible with in-product UART reflashing; that lifecycle is not currently implemented, so this firmware does not claim H2 hardware-rooted authenticity.

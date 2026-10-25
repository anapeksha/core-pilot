# Core Pilot Gateway ESP32-H2 RCP

This is the separately buildable OpenThread UART RCP shipped inside each signed CoreS3 application. It is pinned to ESP-IDF 6.0.2 and uses Spinel over UART0 at 460800 baud.

The CoreS3 side uses RX GPIO10, TX GPIO17, EN GPIO7, and BOOT GPIO18, matching Espressif's M5Stack Thread Border Router configuration. The M5Stack Module Gateway H2 v0.4 schematic routes H2 UART0 as RX GPIO23 and TX GPIO24; these module-specific values and its 2 MiB flash capacity are set in `sdkconfig.defaults`.

Build it independently from the repository root:

```sh
eim run 'idf.py -C firmware/h2 -B firmware/h2/build -D GATEWAY_RCP_MANIFEST_HEX=0000000000000000000000000000000000000000000000000000000000000000 build' v6.0.2
```

Normal CoreS3 builds call `tools/build_gateway_rcp.py`, derive a deterministic recipe digest, build this project, validate the expected sparse artifacts, and generate uncommitted embedding sources under the CoreS3 build directory. The signed CoreS3 OTA image therefore authenticates the exact RCP payload it may install.

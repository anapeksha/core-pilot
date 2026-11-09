# Core Pilot CoreS3 Firmware

This is the ESP-IDF 6.0.2 C++26 application for M5Stack CoreS3. It owns the touchscreen interface, local web server, encrypted storage, networking, Matter controller, Gateway H2 provisioning, and signed OTA updates.

Build the dashboard first, then build the firmware from the repository root:

```sh
cd web
npm ci
npm run build

cd ..
eim run 'idf.py -C firmware/s3 -B firmware/s3/build build' v6.0.2
```

The build also compiles and embeds the ESP32-H2 RCP described in [`../h2/README.md`](../h2/README.md). Hardware flashing and validation must use a positively identified CoreS3.

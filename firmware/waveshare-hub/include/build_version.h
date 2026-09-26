#pragma once
#define ESP_PLANTS_WAVESHARE_VERSION "0.2.0-alpha.35"
#define ESP_PLANTS_WAVESHARE_7_HARDWARE_ID "waveshare-esp32-s3-touch-lcd-7"
#define ESP_PLANTS_WAVESHARE_7B_HARDWARE_ID "waveshare-esp32-s3-touch-lcd-7b"
#ifdef ESP_PLANTS_WAVESHARE_7B
#define ESP_PLANTS_WAVESHARE_HARDWARE_ID ESP_PLANTS_WAVESHARE_7B_HARDWARE_ID
#else
#define ESP_PLANTS_WAVESHARE_HARDWARE_ID ESP_PLANTS_WAVESHARE_7_HARDWARE_ID
#endif
#define ESP_PLANTS_WAVESHARE_PRODUCT_ID "esp-plants-waveshare"
#define ESP_PLANTS_WAVESHARE_RELEASE_CHANNEL "alpha"
#define ESP_PLANTS_WAVESHARE_BUILD_ID "ESPPLANTS-WAVESHARE-" ESP_PLANTS_WAVESHARE_VERSION
#define ESP_PLANTS_WAVESHARE_UPDATER_VERSION 1
#define ESP_PLANTS_WAVESHARE_RELEASE_NOTES \
  "Moves OTA network/TLS work to a persistent PSRAM worker, streams Waveshare firmware through a bounded PSRAM ring to a small internal flash writer, adds measured memory preflight, and retries stale Wi-Fi/DNS/TLS state once without changing saved credentials."
#ifdef ESP_PLANTS_DISTRIBUTION_BUILD
#define ESP_PLANTS_DISTRIBUTION_MARKER "ESP-PLANTS-DISTRIBUTION-BUILD"
#endif

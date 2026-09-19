# M5Stack ESP32-H2 Zigbee firmware

Standalone Zigbee coordinator/translator for the M5Stack Unit Gateway H2 U195.

## Current bring-up scope

- ESP32-H2 native 802.15.4 radio
- Zigbee coordinator role
- M5Stack official 2 MB Zigbee partition layout
- Grove UART on H2 RX GPIO23 / TX GPIO24
- PlantLink UART to the Waveshare
- raw APS packet capture with LQI/RSSI
- permanent sensor identity resolved to IEEE-64
- standard temperature/humidity/battery report decoding
- initial stock HOBEIAN ZG-303Z Tuya 0xEF00 datapoint decoding
- unknown Tuya datapoints stay visible in USB debug and RawZigbeeEvent traffic
- permit-join command from the Waveshare UI

## Decoder strategy

Do not assume every ZG-303Z firmware revision has the same DP table. The H2
normalizes values it recognizes, but keeps the raw packet observable. That means
the first real sensor pairing can correct a firmware-specific mapping without
redesigning the display-side code.

## Destructive reset

PlantLink reserves a factory-reset message, but this bring-up firmware ignores
it. We will not allow a normal firmware update, UART reconnect, or accidental UI
tap to destroy the Zigbee network.

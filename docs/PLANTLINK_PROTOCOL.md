# PlantLink protocol v1

PlantLink is the private UART protocol between the Waveshare ESP32-S3
application controller and the M5Stack ESP32-H2 Zigbee controller.

## Transport

- UART at 115200 8N1 for the initial hardware bring-up
- binary frames
- COBS encoding
- `0x00` frame delimiter
- CRC-32 integrity check
- 16-bit sequence numbers
- maximum payload: 256 bytes in the bring-up implementation

## Decoded frame

| Field | Size |
| --- | ---: |
| protocol version | 1 byte |
| message type | 1 byte |
| flags | 1 byte |
| sequence | 2 bytes |
| payload length | 2 bytes |
| payload | 0-256 bytes |
| CRC-32 | 4 bytes |

Multi-byte integers are little-endian. CRC covers the frame from protocol
version through the final payload byte.

## Bring-up messages

- `Hello` / `HelloAck`
- `Heartbeat`
- `NetworkStatus`
- `PermitJoin`
- `DeviceJoined`
- `DeviceLeft`
- `RemoveDevice`
- `SensorReport`
- `SetSensorOption`
- `CommandResult`
- `RawZigbeeEvent`
- `FactoryResetNetwork`

`FactoryResetNetwork` is reserved but the first H2 firmware deliberately ignores
it. Destructive reset will require an explicit confirmation token before it is
implemented.

## SensorReport v1

The current fixed payload is 21 bytes:

```text
ieee[8]
short_addr u16
field_flags u16
temperature_centi_c i16
humidity_centi_pct u16
soil_moisture_pct u8
battery_pct u8
water_warning u8
lqi u8
rssi_dbm i8
```

Only fields whose validity bits are set in `field_flags` are authoritative.
This lets standard Zigbee clusters and Tuya datapoints arrive at different
times while the H2 maintains one normalized sensor state.

# PlantLink protocol v1

PlantLink is the private UART protocol between the Waveshare ESP32-S3
application controller and the M5Stack ESP32-H2 Zigbee controller.

It deliberately keeps Zigbee/Tuya details out of the Waveshare UI firmware.

## Transport

Initial design:

- full-duplex UART
- binary packets
- COBS encoding
- one `0x00` frame delimiter
- CRC-32 integrity check
- sequence numbers for command/result correlation
- maximum decoded payload: 512 bytes

The initial baud rate will be selected after hardware/documentation review.
Do not treat a baud value as fixed yet.

## Decoded frame

Before COBS encoding, a frame contains:

| Field | Size |
| --- | ---: |
| protocol version | 1 byte |
| message type | 1 byte |
| flags | 1 byte |
| sequence | 2 bytes |
| payload length | 2 bytes |
| payload | 0-512 bytes |
| CRC-32 | 4 bytes |

Multi-byte values are little-endian.

CRC-32 covers the frame from `protocol version` through the final payload byte;
it does not include the CRC field itself or the COBS delimiter.

## Startup handshake

Both processors start with no assumption that the other side is running a
compatible firmware.

The Waveshare sends `Hello`. The H2 returns `HelloAck` containing its
PlantLink protocol version and firmware/capability information. Incompatible
versions must produce an explicit UI/diagnostic state instead of silently
mis-parsing data.

## Core message types

The initial reserved message set is defined in
`shared/plantlink_protocol.h`:

- `Hello` / `HelloAck`
- `Heartbeat`
- `NetworkStatus`
- `PermitJoin`
- `DeviceJoined` / `DeviceLeft`
- `RemoveDevice`
- `SensorReport`
- `SetSensorOption`
- `CommandResult`
- `RawZigbeeEvent`
- `FactoryResetNetwork`

Payload schemas will be added deliberately as each behavior is implemented.
Do not reuse a message ID with incompatible semantics.

## Sensor identity

Every sensor-facing message that identifies a Zigbee device uses its 64-bit
IEEE address as the stable identity. A 16-bit Zigbee short/network address may
be included for diagnostics but is never authoritative.

## Unknown ZG-303Z data

The H2 decoder should preserve observability. Unknown Tuya datapoints or
unexpected Zigbee reports should be available through debug logging and, when
useful, `RawZigbeeEvent` rather than discarded.

## Reset safety

`FactoryResetNetwork` is destructive and must never be triggered as a side
effect of ordinary firmware update, reboot, UART reconnect, or protocol-version
mismatch.

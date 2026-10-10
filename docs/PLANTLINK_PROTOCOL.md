# PlantLink protocol v2

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

## SensorReport v2

The current fixed payload is 30 bytes:

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
route_state u8       # offset 21: UNKNOWN=0, DIRECT=1, ROUTED=2
repeater_ieee[8]     # offsets 22-29, next-hop router IEEE, zero unless ROUTED
```

Only fields whose validity bits are set in `field_flags` are authoritative.
For a normal SensorReport, those bits describe **measurements actually present
in the most recently decoded APS packet**, not everything the H2 has cached.
The H2 retains other previously received values in RAM for diagnostics, but
never marks them as fresh in a new report. A repeated soil value still sets
the soil flag when a genuine new soil report arrives. Temperature-only,
battery-only, and warning-only reports cannot mark soil as newly reported or
advance a plant's phrase. No payload layout or PlantLink version changes.

This lets standard Zigbee clusters and Tuya datapoints arrive at different
times while the H2 maintains one normalized sensor state. Both controllers
should be updated together when moving from the older cached-flag behavior.

## Coordinated v2 update (intentional break)

Both controllers must run v2: Waveshare alpha.39 and H2 alpha.27. The decoder
rejects every other frame version and SensorReport requires exactly 30 bytes.
There is no v1 parser or compatibility bridge. Update both controllers locally
by USB when crossing from v1; an old controller cannot use PlantLink OTA to
update a v2 peer (or vice versa). Preserve existing NVS/plantdata partitions.
The former alpha.23 release bridge has been retired. Package metadata reads the
protocol version from `shared/plantlink/plantlink.h`.

## Route semantics and freshness

Route data describes the coordinator's currently known topology, not a claim
about the RF path taken by an individual incoming packet. DIRECT requires an
IEEE-matching authenticated end-device child in the local neighbor table.
ROUTED requires an active unicast routing-table entry for the sensor and a
valid next-hop router neighbor. The stack's current short-to-IEEE mapping must
match the sensor before either state is trusted. The router must have a nonzero IEEE, outgoing
cost 1-7, age <=3 link-status periods, and must not be a previous or
unauthenticated child. VIA names the next hop from the hub; in a multi-hop mesh
it is not necessarily the sensor's parent. Missing evidence stays UNKNOWN;
absence from the neighbor table alone never means routed. No discovery traffic
is injected and routes present only in remote/source-route tables stay unknown.
SDK table reads occur under the Zigbee lock. Loss detection follows the Zigbee
stack's table aging, not an immediate power-loss notification.

Every actual measurement report includes a fresh table resolution. Every three
seconds H2 also sends SensorReport with frame flag `FlagRouteOnly=0x04` and
`field_flags=0` for sensors known in RAM. This is the same v2 payload/parser.
Waveshare processes only route fields for this flag: it cannot pair a plant,
refresh last-seen/LQI, set current-boot measurement freshness, or rotate phrases.
It lets loss and route changes reach the UI while a sensor is asleep.

The route cache and learned timestamp exist only in RAM. Waveshare resolves the
IEEE against the live repeater registry on every UI refresh, so renames need no
new sensor report. Unknown/offline/unregistered repeaters display blank. Route
and repeater observations expire from the route display after 10 seconds;
link timeout, H2 uptime rollback, and sensor leave also clear route state.
Reboot starts unknown until new topology reports arrive. Sensor/repeater/user
persistence structures and schemas are unchanged.

# Plant protocol v3

The T5 and XIAO currently share protocol version:

```cpp
PROTOCOL_VERSION = 3
```

Magic:

```text
0x504C4E54 = "PLNT"
```

Normal ports:

| Purpose | Port |
|---|---:|
| T5 UDP receive | 42100 |
| sensor UDP local port | 42101 |

Provisioning ESP-NOW channel:

```text
1
```

## Synchronization rule

The protocol header is intentionally copied into both PlatformIO projects so
each project remains independently buildable.

These two files must stay byte-identical:

```text
firmware/t5-hub/include/plant_protocol.h
firmware/xiao-soil-sensor/include/plant_protocol.h
```

Check them with:

```bash
python tools/check_protocol_sync.py
```

## Checksum

Packets use 32-bit FNV-1a over the full packed packet after setting the checksum
field to zero.

A packet is accepted only when:

- magic matches,
- version matches,
- packet_size equals the local packed struct size,
- checksum matches.

## MoistureState

| Numeric | Name |
|---:|---|
| 0 | Unknown |
| 1 | Normal |
| 2 | AlmostDry |
| 3 | Dry |
| 4 | Wet |
| 5 | SensorError |

## ReadingPacket

Packed size: **30 bytes**

| Field | Type | Meaning |
|---|---|---|
| magic | uint32 | packet magic |
| version | uint8 | protocol version |
| packet_size | uint8 | packed struct size |
| sensor_id | uint32 | stable sensor identity |
| sequence | uint32 | report sequence |
| moisture_raw | uint16 | raw/ADC-oriented moisture value |
| moisture_percent | uint8 | 0–100 calculated moisture |
| moisture_state | uint8 enum | current state |
| battery_mv | uint16 | measured battery ADC millivolts |
| battery_percent | uint8 | calculated battery percent |
| reserved_rssi | int8 | reserved/diagnostic field |
| awake_ms | uint32 | sender awake duration |
| checksum | uint32 | FNV-1a checksum |

## AckPacket

Packed size: **23 bytes**

| Field | Type | Meaning |
|---|---|---|
| magic | uint32 | packet magic |
| version | uint8 | protocol version |
| packet_size | uint8 | packed struct size |
| sensor_id | uint32 | target sensor |
| sequence | uint32 | acknowledged reading sequence |
| accepted | uint8 | application acceptance |
| next_wake_seconds | uint32 | legacy/development scheduling hint |
| checksum | uint32 | checksum |

### Scheduling note

Phase 3D+ XIAO firmware intentionally owns its adaptive local-check schedule.

The T5 currently retains `next_wake_seconds` for protocol compatibility, but the
adaptive XIAO does not use that field as its normal scheduling authority.

Do not silently reinterpret this field as a mandatory value without documenting
the architecture change.

## ProvisionPacket

Packed size: **114 bytes**

| Field | Type | Meaning |
|---|---|---|
| magic | uint32 | packet magic |
| version | uint8 | protocol version |
| packet_size | uint8 | packed struct size |
| sensor_id | uint32 | intended sensor |
| wifi_ssid | char[33] | home-Wi-Fi SSID |
| wifi_password | char[65] | home-Wi-Fi password |
| t5_udp_port | uint16 | receiver port |
| checksum | uint32 | checksum |

Security note: current provisioning is not application-layer encrypted or
authenticated.

## ProvisionAckPacket

Packed size: **15 bytes**

| Field | Type | Meaning |
|---|---|---|
| magic | uint32 | packet magic |
| version | uint8 | protocol version |
| packet_size | uint8 | packed struct size |
| sensor_id | uint32 | sensor identity |
| accepted | uint8 | provisioning acceptance |
| checksum | uint32 | checksum |

## Versioning rule

Increase the protocol version when a wire-format or semantic change would make
old and new devices incompatible.

Examples requiring careful version review:

- adding/removing fields,
- changing field size/order,
- changing checksum scheme,
- changing a field's meaning incompatibly.

A UI-only change or plant-name edit does not require a protocol bump.

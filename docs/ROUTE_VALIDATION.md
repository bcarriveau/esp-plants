# PlantLink v2 route validation

Coordinated versions: Waveshare **0.2.0-alpha.39**, H2 **0.2.0-alpha.27**.
Protocol and topology semantics: [PLANTLINK_PROTOCOL.md](PLANTLINK_PROTOCOL.md).

## Local checks (2026-09-26)

- PlatformIO `waveshare_s3_touch_lcd_7`: PASS.
- PlatformIO `m5_gateway_h2`: PASS.
- Python repository regression suite: 98 passed, 1 skipped (existing g++-only
  version-classifier test; g++ is not installed).
- MSVC C++17 `tests/host/test_shared.cpp`: PASS (wire/decoder and Tuya checks).
- MSVC C++17 `tests/host/test_sensor_routes.cpp`: PASS (real resolver, v2 payload
  round-trip, and route formatter with simulated table/registry inputs).
- `tests/test_sensor_route_layout.py`: PASS using installed LVGL font metrics.
- No firmware uploads or physical route verification performed. COM8 emitted
  aircraft-radar logs; it is not an identified H2 connection. COM11 remains the
  configured Waveshare port. 7B runtime is not verified.

The C++ checks can also be built with a host C++17 compiler. Shared tests need
`shared/plantlink` and `shared/zg303z` include paths. Route tests need
`shared/plantlink`, `firmware/m5-h2-zigbee/include`, and
`firmware/waveshare-hub/include`. Run both produced executables.

## Layout bounds

Existing cards remain 500x334 (home) and 772x334 (detail), with screen origin
(14,76) and inherited 24px padding. Montserrat 20 renders WATER ME! at 118px;
horizontal badge padding adds 28px, for 146px total. Centered badges occupy
content x=153..299 (home) and x=289..435 (detail).

The 12px route labels occupy content x=312..452 and x=500..724 respectively,
with fixed 15px height and ellipsis for overflow. Gaps from the badge are 13px
and 65px. Both route labels end at card y=24+292+15=331. Badges end at
y=24+272+22+16=334. Cards end at screen y=410, before navigation starts at 422.
LQI, all metrics, and the Updated label retain their existing geometry.

## Physical test matrix — pending

Update both devices locally by USB for the v1-to-v2 transition. Identify the
H2's own USB port before flashing; do not erase NVS or plantdata. Do not deploy
only one half of this protocol change.

| Scenario | Check on hardware | Host simulation |
| --- | --- | --- |
| Direct | Fresh authenticated local child displays DIRECT TO HUB | Passed |
| Routed | Active route plus live next-hop router displays its registry name | Passed |
| Rename | Rename repeater; route label changes without another sensor measurement | Passed |
| Repeater loss | Power off repeater; label clears after topology ages out, without changing Updated or LQI | Passed |
| Route changes | Move/rejoin sensor; direct -> repeater A -> B -> direct follows current table evidence | Passed |
| Waveshare reboot | Starts blank, then only newly received topology can populate label | Passed |
| H2 reboot | Uptime rollback/link timeout clears route; fresh topology repopulates | Passed |
| Sensor asleep | Topology-only refresh does not advance measurements, freshness, or phrase rotation | Passed |
| Long name + warning | Centered warning never overlaps clipped route label or navigation | Font/bounds passed |

Repeat physical tests with serial logs showing the table evidence and displayed
result. Loss latency depends on Zigbee table aging. UNKNOWN is expected when
there is insufficient local table evidence; see protocol documentation for
multi-hop/source-route limitations. Record results here before calling the
hardware behavior verified.

## Built development image SHA-256

- H2 `.pio/build/m5_gateway_h2/firmware.bin`:
  `eb20b0898245f18e741bb82d88d78bac4d80766b81d01931a9855c5cdb025400`
- Waveshare `.pio/build/waveshare_s3_touch_lcd_7/firmware.bin`:
  `78031d88ff4eac0d4463558fea92e34bf0d1156723532f0af1e43b74d6b14a8f`

These are local development images. No distribution package was published.

# Repository working rules

These rules are part of the project baseline for coding agents and contributors.

## Source of truth

Use this order:

1. current repository files,
2. current intended Git branch,
3. confirmed hardware test results/logs,
4. historical notes.

For current ESP PLANTS development, inspect `waveshare-zigbee` before answering or editing. Never overwrite newer repository work with older chat/context material.

## Active and legacy product lines

The active product line is:

```text
ZG-303Z -> Zigbee -> M5Stack ESP32-H2 -> PlantLink UART -> Waveshare ESP32-S3
```

The Waveshare and H2 are separate PlatformIO projects.

`firmware/t5-hub` and `firmware/xiao-soil-sensor` are preserved legacy product-line sources. Do not apply their Wi-Fi/UDP/ESP-NOW/e-paper assumptions to the current Waveshare/H2 product unless explicitly comparing or maintaining legacy code.

## Active ownership boundary

### Waveshare ESP32-S3

Owns:

- 800x480 UI
- plant names and Zigbee sensor assignments
- user configuration
- current-boot freshness state
- Wi-Fi
- update experience
- persistent user data

### M5Stack ESP32-H2

Owns:

- Zigbee coordinator/network
- permit join and device handling
- ZG-303Z Zigbee/Tuya translation
- Zigbee persistence
- normalized PlantLink reporting

Identify Zigbee sensors permanently by IEEE-64 address. A 16-bit Zigbee network address is transient.

## PlantLink discipline

The authoritative PlantLink protocol definition is:

```text
shared/plantlink/plantlink.h
```

`shared/plantlink_protocol.h` is only a compatibility forwarding include and must not redeclare protocol constants, payload sizes, message IDs, or capabilities.

Protocol changes must:

1. update the authoritative PlantLink definition,
2. preserve compatibility unless an intentional protocol break is documented,
3. update `docs/PLANTLINK_PROTOCOL.md`,
4. update relevant tests,
5. update `CHANGELOG.md` when behavior or compatibility changes,
6. verify both Waveshare and H2 builds before claiming compilation success.

Do not refer to the removed `tools/check_protocol_sync.py`; that belonged to the legacy duplicated T5/XIAO protocol workflow.

## Source-editing rule

Prefer direct edits to real source files.

Do not use pre-build injector/patch scripts to mutate application source, UI layouts, headers, or runtime behavior. Build output must not depend on a one-time source mutation.

## Change discipline

Preserve first. Add second. Redesign only when explicitly requested.

For a narrow fix:

- change only what is required,
- preserve unrelated UI and behavior,
- preserve H2/Zigbee behavior unless necessary,
- preserve persistence formats unless migration is intentionally designed,
- preserve local build/upload settings,
- do not claim a build passed unless it actually ran,
- do not claim hardware verification unless it physically occurred.

## PlatformIO and workspace

The repository root is the permanent VS Code workspace.

Keep Waveshare, H2, T5, and XIAO as independent PlatformIO projects under that workspace. Root `.vscode/tasks.json` is the control layer for local Build/Clean/Upload/Monitor tasks.

Do not silently remove established developer upload configuration. In particular, the current Waveshare 7 environment retains:

```text
upload_port = COM11
```

unless the owner explicitly changes it.

## Waveshare UI rule

Existing working layouts are locked unless redesign is explicitly requested.

For any 800x480 Waveshare UI change, verify bounds mathematically and keep the reserved bottom navigation area intact. A new feature is not successful if it damages an existing working layout.

## Persistence

Normal updates must preserve:

- plant names,
- device name,
- Zigbee sensor assignments,
- repeater/router records,
- Wi-Fi credentials,
- user settings,
- intentionally persistent plant data.

Do not casually change stored record structures. If a schema changes, preserve backward compatibility or add migration logic.

## Wi-Fi and OTA

The Waveshare owns Wi-Fi and release/update behavior.

Waveshare OTA uses the ESP PLANTS package format, A/B app partitions, verified HTTPS, identity checks, package/firmware SHA-256, ESP32-S3 image validation, inactive-slot writes, and final boot-partition activation only after complete validation.

The source contains H2-through-Waveshare OTA support. Treat it as implemented but **not physically verified** unless current repository evidence and actual hardware testing establish otherwise.

## Hardware changes

Do not guess connector pin order, GPIOs, voltages, power sequencing, or board behavior.

Before hardware-critical changes:

1. inspect current source,
2. inspect relevant board/module documentation,
3. prefer physically observed behavior when revisions differ,
4. document the reason,
5. verify on hardware before labeling it proven.

The Waveshare 7B target exists, but do not claim 7B runtime verification until it is physically tested.

## GitHub rule

Firmware builds, tests, uploads, and release packaging are local/manual unless the owner explicitly requests GitHub automation.

Do not add GitHub Actions firmware builds or treat GitHub as a remote firmware build service.

GitHub is read-only for assistant work unless the owner explicitly requests a write.

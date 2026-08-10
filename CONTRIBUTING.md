# Contributing

This project is hardware-first. A code change is not considered fully verified
just because it looks correct in an editor.

## Before editing

Read:

- `AGENTS.md`
- `docs/ARCHITECTURE.md`
- `docs/HARDWARE.md`
- `docs/PROTOCOL.md`
- `docs/BASELINE_STATUS.md`

## Development flow

1. Inspect the actual current files.
2. Make the smallest change that solves the problem.
3. Keep T5 and XIAO PlatformIO projects separate.
4. Run `python tools/check_protocol_sync.py` if protocol files were touched.
5. Build the affected firmware.
6. Test on the actual hardware when hardware behavior changed.
7. Update `CHANGELOG.md` for user-visible behavior.
8. Update documentation when architecture, setup, pins, protocol, or operation
   changes.

## Commit guidance

Prefer small commits with one purpose.

Examples:

```text
sensor: add configurable heartbeat interval
t5: improve setup-page sensor status
protocol: add explicit config packet v4
docs: record watering-watch behavior
```

## Stable release rule

A stable tag should only be created after both firmware projects compile and the
hardware release checklist has been completed.

# Release checklist

Use this before a stable GitHub release/tag.

## Repository

- [ ] working tree clean or intentionally staged.
- [ ] `VERSION` updated.
- [ ] CHANGELOG contains the release entry/date.
- [ ] README current feature list is accurate.
- [ ] protocol documentation matches source.
- [ ] `python tools/check_protocol_sync.py` passes.
- [ ] no private credentials or local logs are committed.
- [ ] license decision has been made for public release.

## Builds

- [ ] clean T5 build passes from `firmware/t5-hub`.
- [ ] clean XIAO build passes from `firmware/xiao-soil-sensor`.
- [ ] build output/version markers saved with release notes when useful.

## Hardware

- [ ] T5 boots and displays.
- [ ] T5 setup mode works.
- [ ] T5 home-Wi-Fi mode works.
- [ ] T5 real shutdown/wake behavior verified.
- [ ] unprovisioned XIAO can be paired.
- [ ] normal UDP reading/ACK verified.
- [ ] deep-sleep timer wake verified.
- [ ] top-button wake verified.
- [ ] service-mode auto sampling verified.
- [ ] 10-second Wi-Fi reset verified.
- [ ] dry/wet calibration verified.
- [ ] calibration persistence verified.
- [ ] watering 5-minute follow-up verified.
- [ ] stable adaptive no-Wi-Fi wake verified.
- [ ] heartbeat behavior verified.

## Multi-sensor

- [ ] at least two sensors coexist without identity mix-up.
- [ ] plant names remain independent on T5.
- [ ] renaming one plant does not require sensor reflash.
- [ ] restart/power cycle preserves names and provisioning.

## Security

- [ ] release notes accurately state provisioning security status.
- [ ] if provisioning is still unencrypted, do not call the release
  production-secure.

## Tag

Only after the above passes:

```bash
git tag -a vX.Y.Z -m "vX.Y.Z"
git push origin vX.Y.Z
```

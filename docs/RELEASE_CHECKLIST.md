# Release checklist

Use this before publishing a stable/current Waveshare/H2 release.

## Repository

- [ ] working tree clean or intentionally staged.
- [ ] `VERSION` matches the intended Waveshare release identity.
- [ ] H2 version is intentionally selected; do not force it to equal the Waveshare version.
- [ ] CHANGELOG contains the relevant release entry/date.
- [ ] README and architecture/security docs describe Waveshare/H2/ZG-303Z as the active product.
- [ ] `python -m pytest tests` passes.
- [ ] documented paths/commands exist.
- [ ] no private credentials or local logs are committed.
- [ ] `firmware/waveshare-hub/platformio.ini` still preserves established local settings, including `upload_port = COM11`, unless deliberately changed.

## Protocol

- [ ] `shared/plantlink/plantlink.h` is the authoritative PlantLink definition.
- [ ] compatibility include `shared/plantlink_protocol.h` does not redeclare protocol constants.
- [ ] PlantLink documentation matches source.
- [ ] Waveshare and H2 agree on required capabilities/message handling.

## Builds

- [ ] clean Waveshare 7 developer build passes.
- [ ] clean H2 developer build passes.
- [ ] `tools\make-waveshare-release.cmd` passes.
- [ ] H2 release image is generated from the current H2 release environment.
- [ ] Waveshare `.plantsota` and manifest are generated from the current release environment.
- [ ] release assets contain the intended product/hardware/build identities.
- [ ] generated hashes match the files being published.
- [ ] no stale binary in `release/` can be confused with the current release.

## Hardware — Waveshare 7 / H2 / ZG-303Z

- [ ] Waveshare 7 boots normally.
- [ ] H2 coordinator starts normally.
- [ ] PlantLink connects.
- [ ] known Zigbee network survives normal reboot.
- [ ] ZG-303Z commissioning works.
- [ ] soil moisture reports.
- [ ] temperature reports.
- [ ] air humidity reports.
- [ ] battery reports.
- [ ] stable IEEE-64 assignment is retained.
- [ ] after Waveshare reboot, not-yet-reported sensors remain WAITING.
- [ ] watering summaries exclude stale pre-reboot readings.

## Wi-Fi

- [ ] scan/select flow works.
- [ ] QR setup works.
- [ ] failed replacement credentials do not destroy known-good credentials.
- [ ] Disconnect Wi-Fi does not erase credentials.
- [ ] Forget Wi-Fi confirms and erases credentials.
- [ ] device remains usable offline.

## Waveshare OTA

- [ ] verified HTTPS is active.
- [ ] manifest/package identity validation passes.
- [ ] package and firmware SHA-256 checks pass.
- [ ] ESP32-S3 image validation passes.
- [ ] bounded download/redirect behavior is preserved.
- [ ] inactive-slot write is confirmed.
- [ ] activation occurs only after successful validation.
- [ ] user settings/data survive.

## H2-through-Waveshare OTA

Do not check these off from source inspection alone.

- [ ] physically tested on the actual Waveshare + M5Stack H2 pair.
- [ ] H2 image transfer succeeds.
- [ ] H2 validates and activates the inactive slot.
- [ ] expected H2 build is reported after reboot.
- [ ] Waveshare stage follows only after H2 success.
- [ ] failed H2 stage blocks Waveshare installation.
- [ ] Zigbee network survives.
- [ ] user configuration survives.

Until those checks are physically completed, release notes must not call H2-through-Waveshare OTA hardware-verified.

## Waveshare 7B

- [ ] build compatibility checked if the release intends to include 7B artifacts.
- [ ] physical 7B runtime testing completed before claiming runtime verification.

## Tag / publish

Only after the intended release gate passes:

```bash
git tag -a vX.Y.Z -m "vX.Y.Z"
git push origin vX.Y.Z
```

GitHub release assets must come from the current release tooling; do not republish historical binaries as current.

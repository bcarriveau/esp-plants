# Phase 2 — H2 firmware through the Waveshare

## Status

The current `waveshare-zigbee` source contains the Phase 2 H2 OTA implementation.

The release flow builds an H2 distribution image first, records its identity/hash metadata in the Waveshare manifest, and lets the Waveshare transfer that image over PlantLink before installing its own update.

**Implementation in source is not the same as physical verification. This repository does not claim the H2-through-Waveshare path is hardware-verified yet.**

## H2 update model

The H2 uses its A/B application layout.

PlantLink OTA messages carry the begin/chunk/end/status/abort flow. The receiver validates the incoming image before activation, including the expected build/distribution identity and ESP image compatibility. It writes the inactive application partition and changes the boot partition only after successful completion.

No normal H2 OTA flow should erase Zigbee/NVS state.

## Waveshare orchestration

The Waveshare release installer performs the H2 stage before its own application stage.

Expected release order:

1. obtain current release manifest over verified HTTPS,
2. validate intended H2 metadata,
3. download/verify the H2 release asset,
4. transfer H2 firmware over PlantLink,
5. wait for H2 reboot/expected build,
6. only then proceed with the normal Waveshare A/B `.plantsota` install.

If the H2 stage fails, the Waveshare application stage must not proceed as if `Update All` succeeded.

## Release tooling

Use:

```text
tools\make-waveshare-release.cmd
```

It builds the H2 release environment before the Waveshare release environment.

Current release output includes:

```text
esp-plants-h2-<H2_VERSION>.bin
esp-plants-h2-<H2_VERSION>.bin.sha256
esp-plants-waveshare-<WAVESHARE_VERSION>.plantsota
esp-plants-waveshare.manifest.json
```

Do not use historical binaries left from an older alpha as the current H2 release.

## Physical verification still required

A hardware verification pass should prove:

- complete H2 transfer over PlantLink,
- correct inactive-slot selection,
- image/hash/build/distribution validation,
- H2 reboot into the intended build,
- Zigbee network persistence,
- Waveshare stage blocked on H2 failure,
- Waveshare stage allowed after H2 success,
- all Waveshare user configuration preserved.

Record the exact hardware/build identities used when this is tested. Do not backfill a verification claim from source inspection alone.

## Historical note

Earlier alpha-specific Phase 2 bring-up notes described the one-time bootstrap concept. Those version-specific instructions are no longer the current release procedure; current behavior and release identities must come from the live branch source and release tooling.

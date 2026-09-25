# ESP PLANTS release output

`release/` is generated-output space for the **current** Waveshare/H2 release. It is not an archive of old firmware.

Do not hand-create, rename, or reuse historical OTA/H2 binaries as current release assets.

## Build the current release

From Windows, run:

```text
tools\make-waveshare-release.cmd
```

The script builds the H2 distribution image first and then the Waveshare distribution package/manifest.

Expected outputs:

```text
esp-plants-h2-<H2_VERSION>.bin
esp-plants-h2-<H2_VERSION>.bin.sha256
esp-plants-waveshare-<WAVESHARE_VERSION>.plantsota
esp-plants-waveshare.manifest.json
```

The Waveshare packaging logic validates the distribution provenance/build identity and includes H2 metadata in the manifest.

Only publish assets generated from the intended current source/version. Remove or archive stale local outputs before preparing a release so they cannot be mistaken for the current firmware.

H2-through-Waveshare OTA support exists in source, but physical hardware verification must be documented separately before release notes call that path verified.

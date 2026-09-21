# ESP PLANTS release output

Do not hand-create or rename OTA assets.

Build the public release environment:

```text
pio run -d firmware/waveshare-hub -e waveshare_s3_touch_lcd_7_release
```

or run `tools/make-waveshare-release.cmd` on Windows.

The build must produce exactly:

```text
esp-plants-waveshare-<VERSION>.plantsota
esp-plants-waveshare.manifest.json
```

Upload both files to GitHub Release tag `v<VERSION>`.

The packaging script refuses an ordinary developer firmware image that does not
contain the `ESP-PLANTS-DISTRIBUTION-BUILD` provenance marker.

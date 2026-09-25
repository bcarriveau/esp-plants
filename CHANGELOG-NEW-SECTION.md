### Waveshare 7B target and workspace integration

#### Added

- Added a dedicated `waveshare_s3_touch_lcd_7b` PlatformIO environment so the Waveshare ESP32-S3 Touch LCD 7B can share the ESP PLANTS application while retaining a board-specific compile target.
- Added root VS Code Build, Clean, Upload, and Monitor tasks for the Waveshare 7B.
- Added combined `Build Waveshare 7B + H2` and `Clean Waveshare 7B + H2` workspace tasks.
- Added the `ESP_PLANTS_WAVESHARE_7B` build define as the boundary for 7B-specific hardware handling without duplicating the complete application.

#### Preserved

- The existing Waveshare 7 remains the default Waveshare PlatformIO environment and the default Waveshare + H2 VS Code build path.
- Existing Waveshare 7 build/upload configuration remains intact while the 7B uses its own PlatformIO environment.
- Machine-specific serial-port assignments remain local development configuration rather than project-level behavior.
- Existing 800x480 ESP PLANTS application/UI behavior remains shared rather than introducing a second independent interface.
- H2 Zigbee firmware and ZG-303Z sensor behavior are unchanged by the 7B scaffold.

#### Verification status

- The 7B target is scaffolded in source and workspace tooling.
- No physical ESP PLANTS runtime verification on the Waveshare 7B is claimed yet.
- The existing Waveshare 7 + M5Stack H2 + ZG-303Z hardware path remains the physically verified baseline.


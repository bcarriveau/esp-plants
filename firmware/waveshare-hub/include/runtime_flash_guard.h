#pragma once

namespace espplants_runtime_flash {

// Call once after lcd_init() has completed. Boot-time migration writes that
// happen before this point intentionally do not trigger display recovery.
void markDisplayReady();

// Explicit safety-net request for heavy runtime activity such as an OTA check
// reaching its idle state. Multiple requests coalesce into one recovery.
void requestDisplayRecovery(const char *reason);

// Run from the normal Arduino loop. On the original 800x480 Waveshare this is
// deliberately a no-op; on the 7B it queues Espressif's VSYNC-aligned RGB DMA
// restart without resetting the LCD or blanking the backlight.
void serviceDisplayRecovery();

}  // namespace espplants_runtime_flash

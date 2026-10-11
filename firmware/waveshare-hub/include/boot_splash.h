#pragma once

// Temporary boot-only LVGL overlay. The application pages/navigation remain
// owned by EEZ Studio; this overlay is removed automatically after startup.
namespace espplants_boot_splash {
void show(const char *deviceName, bool lightAppearance);
void animateAndDismiss();

// Call only while holding the LVGL port lock. Returns false once dismissed.
// Report real state; no connection is inferred from animation progress.
bool update(bool h2Linked, bool zigbeeReady,
            bool wifiConfigured, bool wifiConnected,
            bool wifiReconnectSuppressed, bool systemReady);
}  // namespace espplants_boot_splash

#pragma once

// Boot-only photo layer. EEZ pages are not built until the timed splash ends.
// All functions except readyToOpenDashboard() are called with the LVGL lock.
namespace espplants_boot_splash {
void show(const char *deviceName, bool lightAppearance);
void startAnimation();
bool active();
bool readyToOpenDashboard();
void dismiss();

// Live status only; H2 UART presence and Zigbee network readiness are distinct.
bool update(bool h2Linked, bool zigbeeReady,
            bool wifiConfigured, bool wifiConnected,
            bool wifiReconnectSuppressed, bool systemReady);
}  // namespace espplants_boot_splash

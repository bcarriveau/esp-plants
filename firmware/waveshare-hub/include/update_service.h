#pragma once

#include <Arduino.h>

namespace espplants_update {

// 7B boot diagnostic: optionally postpone only the Wi-Fi station startup.
// The updater, persisted credentials and time seed initialize immediately.
void begin(bool deferWifiUntilPlantGrown = false);
void service();

void startWifiSetup();
void cancelWifiSetup();
void disconnectWifi();
void reconnectWifi();
void forgetWifi();
void requestCheck();
void requestInstall();

bool wifiConfigured();
bool wifiConnected();
bool setupPortalActive();
bool wifiReconnectSuppressed();
const char *wifiSsid();
const char *wifiAddress();
const char *setupSsid();
const char *setupPassword();

bool checking();
bool installing();
bool updateAvailable();
bool releaseNotesAvailable();
const char *releaseNotes();
int updateProgress();
const char *currentVersion();
const char *latestVersion();
const char *statusText();

}  // namespace espplants_update

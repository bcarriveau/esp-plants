#pragma once

#include <Arduino.h>

namespace espplants_update {

void begin();
void service();

void startWifiSetup();
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
int updateProgress();
const char *currentVersion();
const char *latestVersion();
const char *statusText();

}  // namespace espplants_update

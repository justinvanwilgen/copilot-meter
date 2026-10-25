#pragma once

#include <Arduino.h>

#include "config.h"

namespace wifisetup
{
struct Events
{
  bool connected;      // AP portal joined a network
  bool portalTimedOut; // AP portal closed after its timeout
  bool settingsSaved;  // settings form submitted (AP or LAN)
};

// Registers the settings form backed by `cfg`; the same object is updated in place on save.
void begin(AppConfig &cfg);
// Re-reads `cfg` into the form after it was changed elsewhere (e.g. serial console).
void syncForm();

bool hasSavedCredentials();
String savedSsid();
void eraseCredentials();

// Non-blocking captive portal on our own AP. timeoutSeconds = 0 keeps it open indefinitely.
void startAccessPoint(uint16_t timeoutSeconds);
void stopAccessPoint();
bool accessPointActive();
const char *apName();
const char *apPassword();
uint32_t accessPointSecondsLeft();

// Settings page served on the home network (http://<ip>/param).
void startLanPortal();
void stopLanPortal();
bool lanPortalActive();

// Call every loop.
Events process();
} // namespace wifisetup

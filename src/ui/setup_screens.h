#pragma once

#include <stdint.h>

// Full-screen setup/status screens. Each "show" call redraws the whole screen.
void screenConnecting(const char *ssid);
void screenConnectingTick(uint32_t elapsedMs);

void screenAccessPoint(const char *apName, const char *apPassword, const char *reason);
// secondsLeft < 0 hides the countdown.
void screenAccessPointCountdown(int32_t secondsLeft);

void screenFinishSetup(const char *ssid, const char *ip);

void screenMessage(const char *title, const char *line1, const char *line2);

#pragma once

#include <stdint.h>

#include "../app/usage_math.h"

struct DashboardData
{
  bool hasUsage;
  double used;
  uint32_t limit;

  bool timeValid;
  MonthInfo month;

  bool wifiConnected;
  int32_t rssi;

  char updated[32];    // e.g. "Updated 21:34"
  char status[40];     // right side of the status bar
  uint16_t statusColor;
};

// Allocates the mascot sprite; call once after displayInit(). Returns false if out of memory.
bool dashboardBegin();
// Clears the screen and draws the dashboard chrome; subsequent updates only redraw what changed.
void dashboardShow();
void dashboardHide();
bool dashboardVisible();
void dashboardUpdate(const DashboardData &data);
// Advances the mascot animation; call every loop.
void dashboardTick(uint32_t nowMs);
Mood dashboardMood();

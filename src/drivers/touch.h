#pragma once

#include <stdint.h>

struct TouchPoint
{
  int16_t x;
  int16_t y;
  uint16_t z;
};

void touchInit();

// Screen coordinates, following the current tft rotation. Returns false when not pressed.
bool touchRead(TouchPoint &p);

// Raw 12-bit XPT2046 readings, useful for calibration.
bool touchReadRaw(uint16_t &x, uint16_t &y, uint16_t &z);

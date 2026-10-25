#pragma once

#include <TFT_eSPI.h>

extern TFT_eSPI tft;

// Rotation: 0/2 portrait, 1/3 landscape (1 = USB port on the right).
void displayInit(uint8_t rotation = 1);
void displaySetBrightness(uint8_t level);
uint8_t displayGetBrightness();

#pragma once

#include <TFT_eSPI.h>

// Draws `text` as a QR code (with a white quiet zone) whose side is at most maxSize pixels.
// Returns the side length drawn, or 0 if encoding failed.
int16_t drawQrCode(TFT_eSPI &tft, const char *text, int16_t x, int16_t y, int16_t maxSize);

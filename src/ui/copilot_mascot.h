#pragma once

#include <TFT_eSPI.h>

#include "../app/usage_math.h"

constexpr int16_t MASCOT_W = 150;
constexpr int16_t MASCOT_H = 150;
constexpr uint16_t MASCOT_FPS = 12;

// Draws animation frame `frame` of the Copilot-style pilot for `mood` into a MASCOT_W x MASCOT_H canvas.
void mascotRender(TFT_eSprite &canvas, Mood mood, uint32_t frame, uint16_t background);

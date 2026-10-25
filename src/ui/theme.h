#pragma once

#include <stdint.h>

#include "../app/usage_math.h"

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// GitHub dark palette
namespace theme
{
constexpr uint16_t BG = rgb565(13, 17, 23);
constexpr uint16_t PANEL = rgb565(22, 27, 34);
constexpr uint16_t BORDER = rgb565(48, 54, 61);
constexpr uint16_t TEXT = rgb565(230, 237, 243);
constexpr uint16_t MUTED = rgb565(139, 148, 158);
constexpr uint16_t ACCENT = rgb565(137, 87, 229);
constexpr uint16_t LINK = rgb565(88, 166, 255);
constexpr uint16_t GREEN = rgb565(63, 185, 80);
constexpr uint16_t TEAL = rgb565(57, 197, 187);
constexpr uint16_t YELLOW = rgb565(210, 153, 34);
constexpr uint16_t ORANGE = rgb565(219, 109, 40);
constexpr uint16_t RED = rgb565(248, 81, 73);
constexpr uint16_t DEEP_RED = rgb565(218, 54, 51);
constexpr uint16_t WHITE = rgb565(255, 255, 255);
constexpr uint16_t BLACK = 0x0000;

inline uint16_t moodColor(Mood mood)
{
  switch (mood)
  {
  case Mood::Happy: return GREEN;
  case Mood::Cruising: return TEAL;
  case Mood::Focused: return YELLOW;
  case Mood::Worried: return ORANGE;
  case Mood::Panic: return RED;
  case Mood::BurntOut: return DEEP_RED;
  }
  return TEXT;
}
} // namespace theme

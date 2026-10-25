#include "dashboard.h"

#include <Arduino.h>

#include "../drivers/display.h"
#include "copilot_mascot.h"
#include "theme.h"

static constexpr int16_t MASCOT_X = 4;
static constexpr int16_t MASCOT_Y = 30;
static constexpr int16_t PANEL_X = 160;
static constexpr int16_t PANEL_W = 154;
static constexpr int16_t BAR_Y = 136;
static constexpr int16_t BAR_H = 12;
static constexpr int16_t MARKER_OVERHANG = 3;
static constexpr int16_t STATUS_Y = 216;

static TFT_eSprite mascotSprite(&tft);
static bool spriteReady = false;
static bool visible = false;
static Mood mood = Mood::Cruising;
static uint32_t frame = 0;
static uint32_t lastFrameAt = 0;

struct Field
{
  char text[48];
  uint16_t color;
  bool drawn;
};

enum FieldId : uint8_t
{
  F_MONTH,
  F_USED,
  F_LIMIT,
  F_PERCENT,
  F_PACE,
  F_PROJECTION,
  F_RESET,
  F_MOOD,
  F_UPDATED,
  F_STATUS,
  F_COUNT
};

static Field fields[F_COUNT];

struct BarState
{
  int16_t fill;
  int16_t marker;
  uint16_t color;
  bool drawn;
};
static BarState bar;
static int8_t wifiBars = -2;

enum class FontId : uint8_t
{
  Small,
  Medium,
  Bold18,
  Bold24
};

static void useFont(FontId font)
{
  switch (font)
  {
  case FontId::Small: tft.setTextFont(2); break;
  case FontId::Medium: tft.setTextFont(4); break;
  case FontId::Bold18: tft.setFreeFont(&FreeSansBold18pt7b); break;
  case FontId::Bold24: tft.setFreeFont(&FreeSansBold24pt7b); break;
  }
}

// Redraws a text field only when its content or colour changed, clearing just that box.
static void drawField(FieldId id, const char *text, uint16_t color, int16_t x, int16_t y, int16_t w, int16_t h,
                      FontId font, uint8_t datum, uint16_t bg = theme::BG)
{
  Field &f = fields[id];
  if (f.drawn && f.color == color && strcmp(f.text, text) == 0)
    return;
  strlcpy(f.text, text, sizeof(f.text));
  f.color = color;
  f.drawn = true;

  tft.fillRect(x, y, w, h, bg);
  useFont(font);
  tft.setTextColor(color);
  tft.setTextDatum(datum);
  int16_t ax = x;
  if (datum == TC_DATUM)
    ax = x + w / 2;
  else if (datum == TR_DATUM)
    ax = x + w;
  tft.drawString(text, ax, y);
  tft.setTextDatum(TL_DATUM);
}

static void drawBar(float percent, float expectedPercent, bool showMarker, uint16_t color)
{
  const int16_t inner = PANEL_W - 2;
  const float clamped = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
  const int16_t fill = static_cast<int16_t>(inner * clamped / 100.0f + 0.5f);
  const int16_t marker = showMarker ? static_cast<int16_t>(inner * (expectedPercent > 100 ? 100 : expectedPercent) / 100.0f) : -1;
  if (bar.drawn && bar.fill == fill && bar.marker == marker && bar.color == color)
    return;
  bar = {fill, marker, color, true};

  tft.fillRect(PANEL_X, BAR_Y - MARKER_OVERHANG, PANEL_W, BAR_H + MARKER_OVERHANG * 2, theme::BG);
  tft.drawRect(PANEL_X, BAR_Y, PANEL_W, BAR_H, theme::BORDER);
  tft.fillRect(PANEL_X + 1, BAR_Y + 1, inner, BAR_H - 2, theme::PANEL);
  if (fill > 0)
    tft.fillRect(PANEL_X + 1, BAR_Y + 1, fill, BAR_H - 2, color);
  if (marker >= 0)
    tft.fillRect(PANEL_X + marker, BAR_Y - MARKER_OVERHANG, 2, BAR_H + MARKER_OVERHANG * 2, theme::TEXT);
}

static int8_t barsForRssi(bool connected, int32_t rssi)
{
  if (!connected) return -1;
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -85) return 1;
  return 0;
}

static void drawWifiIcon(int8_t bars)
{
  if (bars == wifiBars)
    return;
  wifiBars = bars;
  const int16_t x = 6;
  const int16_t base = STATUS_Y + 19;
  tft.fillRect(x, STATUS_Y + 2, 20, 21, theme::PANEL);
  for (int8_t i = 0; i < 4; ++i)
  {
    const int16_t h = 4 + i * 4;
    tft.fillRect(x + i * 4, base - h, 3, h, i < bars ? theme::TEXT : theme::BORDER);
  }
  if (bars < 0)
  {
    tft.drawLine(x + 9, STATUS_Y + 5, x + 17, STATUS_Y + 13, theme::RED);
    tft.drawLine(x + 17, STATUS_Y + 5, x + 9, STATUS_Y + 13, theme::RED);
  }
}

bool dashboardBegin()
{
  // 16-bit keeps the purple shades accurate; fall back to 8-bit if the heap is tight.
  mascotSprite.setColorDepth(16);
  spriteReady = mascotSprite.createSprite(MASCOT_W, MASCOT_H) != nullptr;
  if (!spriteReady)
  {
    mascotSprite.setColorDepth(8);
    spriteReady = mascotSprite.createSprite(MASCOT_W, MASCOT_H) != nullptr;
  }
  Serial.printf("[ui] mascot sprite %s (%d-bit), heap %u\n", spriteReady ? "ready" : "FAILED",
                mascotSprite.getColorDepth(), ESP.getFreeHeap());
  return spriteReady;
}

void dashboardShow()
{
  visible = true;
  memset(fields, 0, sizeof(fields));
  bar.drawn = false;
  wifiBars = -2;

  tft.fillScreen(theme::BG);
  tft.setTextFont(2);
  tft.setTextColor(theme::MUTED);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("GitHub Copilot", 8, 8);
  tft.fillRect(0, STATUS_Y, tft.width(), tft.height() - STATUS_Y, theme::PANEL);
  tft.drawFastHLine(0, STATUS_Y, tft.width(), theme::BORDER);
  lastFrameAt = 0;
}

void dashboardHide()
{
  visible = false;
}

bool dashboardVisible()
{
  return visible;
}

Mood dashboardMood()
{
  return mood;
}

void dashboardUpdate(const DashboardData &d)
{
  if (!visible)
    return;

  static const char *MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  char text[48];

  if (d.timeValid && d.month.month >= 1 && d.month.month <= 12)
    snprintf(text, sizeof(text), "AI CREDITS  %s %d", MONTHS[d.month.month - 1], d.month.year);
  else
    snprintf(text, sizeof(text), "AI CREDITS");
  drawField(F_MONTH, text, theme::MUTED, PANEL_X, 8, PANEL_W, 16, FontId::Small, TL_DATUM);

  const float percent = d.hasUsage ? usagePercent(d.used, d.limit) : 0.0f;
  const Mood newMood = d.hasUsage ? moodForPercent(percent) : Mood::Cruising;
  const uint16_t moodColor = d.hasUsage ? theme::moodColor(newMood) : theme::MUTED;
  if (newMood != mood)
  {
    mood = newMood;
    frame = 0;
  }

  // Used credits: the biggest font that fits the panel
  if (d.hasUsage)
    formatThousands(d.used, text, sizeof(text));
  else
    strlcpy(text, "--", sizeof(text));
  useFont(FontId::Bold24);
  const FontId usedFont = tft.textWidth(text) <= PANEL_W ? FontId::Bold24 : FontId::Bold18;
  drawField(F_USED, text, theme::TEXT, PANEL_X, 26, PANEL_W, 46, usedFont, TL_DATUM);

  char limitText[24];
  formatThousands(d.limit, limitText, sizeof(limitText));
  snprintf(text, sizeof(text), "of %s", limitText);
  drawField(F_LIMIT, text, theme::MUTED, PANEL_X, 72, PANEL_W, 26, FontId::Medium, TL_DATUM);

  if (d.hasUsage)
    snprintf(text, sizeof(text), percent < 100.0f ? "%.1f%%" : "%.0f%%", percent);
  else
    strlcpy(text, "--%", sizeof(text));
  drawField(F_PERCENT, text, moodColor, PANEL_X, 100, PANEL_W, 28, FontId::Bold18, TL_DATUM);

  PaceInfo pace{Pace::Unknown, 0, 0, false, 0};
  if (d.timeValid)
    pace = computePace(d.hasUsage ? d.used : 0, d.limit, d.month.elapsedFraction);
  drawBar(percent, pace.expectedPercent, d.timeValid, moodColor);

  uint16_t paceColor = theme::MUTED;
  if (!d.hasUsage || !d.timeValid)
    strlcpy(text, "Pace: --", sizeof(text));
  else if (pace.pace == Pace::Fast)
  {
    snprintf(text, sizeof(text), "Burning fast +%.0f%%", pace.deltaPercent);
    paceColor = theme::ORANGE;
  }
  else if (pace.pace == Pace::Under)
  {
    snprintf(text, sizeof(text), "Under pace %.0f%%", pace.deltaPercent);
    paceColor = theme::TEAL;
  }
  else if (pace.pace == Pace::OnTrack)
  {
    strlcpy(text, "On track", sizeof(text));
    paceColor = theme::GREEN;
  }
  else
    strlcpy(text, "Pace: too early", sizeof(text));
  drawField(F_PACE, text, paceColor, PANEL_X, 154, PANEL_W, 16, FontId::Small, TL_DATUM);

  uint16_t projColor = theme::MUTED;
  if (d.hasUsage && pace.projectionValid)
  {
    char compact[16];
    formatCompact(pace.projected, compact, sizeof(compact));
    snprintf(text, sizeof(text), "Month end: ~%s", compact);
    if (d.limit > 0 && pace.projected > d.limit)
      projColor = theme::RED;
  }
  else
    text[0] = '\0';
  drawField(F_PROJECTION, text, projColor, PANEL_X, 171, PANEL_W, 16, FontId::Small, TL_DATUM);

  if (d.timeValid)
  {
    char duration[24];
    formatDuration(d.month.secondsToReset, duration, sizeof(duration));
    snprintf(text, sizeof(text), "Resets in %s", duration);
  }
  else
    text[0] = '\0';
  drawField(F_RESET, text, theme::MUTED, PANEL_X, 188, PANEL_W, 16, FontId::Small, TL_DATUM);

  drawField(F_MOOD, d.hasUsage ? moodName(mood) : "", moodColor, MASCOT_X, 184, MASCOT_W, 26, FontId::Medium,
            TC_DATUM);

  drawWifiIcon(barsForRssi(d.wifiConnected, d.rssi));
  drawField(F_UPDATED, d.updated, theme::MUTED, 30, STATUS_Y + 4, 120, 16, FontId::Small, TL_DATUM, theme::PANEL);
  drawField(F_STATUS, d.status, d.statusColor, 150, STATUS_Y + 4, tft.width() - 156, 16, FontId::Small, TR_DATUM,
            theme::PANEL);
}

void dashboardTick(uint32_t nowMs)
{
  if (!visible || !spriteReady)
    return;
  if (lastFrameAt != 0 && nowMs - lastFrameAt < 1000 / MASCOT_FPS)
    return;
  lastFrameAt = nowMs;
  mascotRender(mascotSprite, mood, frame++, theme::BG);
  mascotSprite.pushSprite(MASCOT_X, MASCOT_Y);
}

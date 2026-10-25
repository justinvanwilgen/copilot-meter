#include "setup_screens.h"

#include <Arduino.h>

#include "../drivers/display.h"
#include "qr.h"
#include "theme.h"

static constexpr int16_t QR_X = 8;
static constexpr int16_t QR_Y = 36;
static constexpr int16_t QR_MAX = 140;
static constexpr int16_t COL_X = 158;
static constexpr int16_t FOOTER_Y = 198;

static void header(const char *title)
{
  tft.fillScreen(theme::BG);
  tft.fillRect(0, 0, tft.width(), 28, theme::PANEL);
  tft.drawFastHLine(0, 28, tft.width(), theme::BORDER);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(theme::ACCENT);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.drawString("Copilot Meter", 8, 14);
  tft.setTextColor(theme::MUTED);
  tft.setTextFont(2);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(title, tft.width() - 8, 14);
  tft.setTextDatum(TL_DATUM);
}

static int16_t line(const char *text, int16_t y, uint16_t color, uint8_t font = 2)
{
  tft.setTextFont(font);
  tft.setTextColor(color);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(text, COL_X, y);
  return y + tft.fontHeight() + 1;
}

void screenConnecting(const char *ssid)
{
  header("Connecting");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(theme::TEXT);
  tft.setTextFont(4);
  tft.drawString("Connecting to WiFi", tft.width() / 2, 96);
  tft.setTextFont(2);
  tft.setTextColor(theme::LINK);
  tft.drawString(ssid && ssid[0] ? ssid : "(saved network)", tft.width() / 2, 126);
  tft.setTextColor(theme::MUTED);
  tft.drawString("Hold BOOT for 3 s to change WiFi", tft.width() / 2, 214);
  tft.setTextDatum(TL_DATUM);
}

void screenConnectingTick(uint32_t elapsedMs)
{
  const uint8_t active = (elapsedMs / 250) % 5;
  for (uint8_t i = 0; i < 5; ++i)
  {
    const int16_t x = tft.width() / 2 - 40 + i * 20;
    tft.fillRect(x - 4, 150, 8, 8, i == active ? theme::ACCENT : theme::BORDER);
  }
}

void screenAccessPoint(const char *apName, const char *apPassword, const char *reason)
{
  header("WiFi setup");

  char wifiQr[96];
  snprintf(wifiQr, sizeof(wifiQr), "WIFI:T:WPA;S:%s;P:%s;;", apName, apPassword);
  drawQrCode(tft, wifiQr, QR_X, QR_Y, QR_MAX);

  int16_t y = 36;
  y = line("1. Scan or join WiFi:", y, theme::MUTED);
  y = line(apName, y, theme::TEXT);
  y = line("Password:", y + 2, theme::MUTED);
  y = line(apPassword, y, theme::YELLOW, 4);
  y = line("2. Page opens, or visit", y + 4, theme::MUTED);
  y = line("192.168.4.1", y, theme::LINK);
  y = line("3. Pick WiFi, add your", y + 4, theme::MUTED);
  line("GitHub user + token", y, theme::MUTED);

  tft.setTextFont(2);
  tft.setTextColor(theme::ORANGE);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(reason, 8, FOOTER_Y);
}

void screenAccessPointCountdown(int32_t secondsLeft)
{
  tft.fillRect(0, FOOTER_Y + 18, tft.width(), 22, theme::BG);
  if (secondsLeft < 0)
    return;
  char text[64];
  snprintf(text, sizeof(text), "Retrying saved WiFi in %ld:%02ld", static_cast<long>(secondsLeft / 60),
           static_cast<long>(secondsLeft % 60));
  tft.setTextFont(2);
  tft.setTextColor(theme::MUTED);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(text, 8, FOOTER_Y + 18);
}

void screenFinishSetup(const char *ssid, const char *ip)
{
  header("Almost there");

  char url[48];
  snprintf(url, sizeof(url), "http://%s/param", ip);
  drawQrCode(tft, url, QR_X, QR_Y, QR_MAX);

  char host[40];
  snprintf(host, sizeof(host), "http://%s", ip);
  int16_t y = 36;
  y = line("Connected to", y, theme::MUTED);
  y = line(ssid, y, theme::GREEN);
  y = line("On that network open:", y + 6, theme::MUTED);
  y = line(host, y, theme::LINK);
  y = line("-> Setup", y, theme::LINK);
  y = line("Add GitHub username", y + 6, theme::MUTED);
  y = line("+ fine-grained token", y, theme::MUTED);
  line("(Plan: read-only)", y, theme::MUTED);

  tft.setTextFont(2);
  tft.setTextColor(theme::MUTED);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("or http://copilot-meter.local", 8, FOOTER_Y);
  tft.setTextColor(theme::ORANGE);
  tft.drawString("Can't reach it? Tap here for setup hotspot", 8, FOOTER_Y + 18);
}

void screenMessage(const char *title, const char *line1, const char *line2)
{
  header(title);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(theme::TEXT);
  tft.setTextFont(4);
  tft.drawString(line1, tft.width() / 2, 100);
  tft.setTextFont(2);
  tft.setTextColor(theme::MUTED);
  tft.drawString(line2, tft.width() / 2, 132);
  tft.setTextDatum(TL_DATUM);
}

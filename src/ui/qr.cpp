#include "qr.h"

#include <qrcode.h>

static constexpr int16_t QUIET_MODULES = 2;

// esp_qrcode only passes the encoded symbol to its callback, so drawing parameters live here.
static TFT_eSPI *target = nullptr;
static int16_t originX = 0;
static int16_t originY = 0;
static int16_t maxSide = 0;
static int16_t drawnSide = 0;

static void drawModules(esp_qrcode_handle_t qrcode)
{
  const int16_t modules = esp_qrcode_get_size(qrcode);
  const int16_t scale = maxSide / (modules + QUIET_MODULES * 2);
  if (scale < 1)
    return;

  drawnSide = (modules + QUIET_MODULES * 2) * scale;
  target->fillRect(originX, originY, drawnSide, drawnSide, TFT_WHITE);
  const int16_t left = originX + QUIET_MODULES * scale;
  const int16_t top = originY + QUIET_MODULES * scale;
  for (int16_t my = 0; my < modules; ++my)
  {
    int16_t mx = 0;
    while (mx < modules)
    {
      if (!esp_qrcode_get_module(qrcode, mx, my))
      {
        ++mx;
        continue;
      }
      int16_t run = 1;
      while (mx + run < modules && esp_qrcode_get_module(qrcode, mx + run, my))
        ++run;
      target->fillRect(left + mx * scale, top + my * scale, run * scale, scale, TFT_BLACK);
      mx += run;
    }
  }
}

int16_t drawQrCode(TFT_eSPI &tft, const char *text, int16_t x, int16_t y, int16_t maxSize)
{
  target = &tft;
  originX = x;
  originY = y;
  maxSide = maxSize;
  drawnSide = 0;

  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = drawModules;
  cfg.max_qrcode_version = 6;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  if (esp_qrcode_generate(&cfg, text) != ESP_OK)
    return 0;
  return drawnSide;
}

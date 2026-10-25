#include "display.h"

static constexpr uint8_t BACKLIGHT_CHANNEL = 0;
static constexpr uint32_t BACKLIGHT_PWM_HZ = 5000;
static constexpr uint8_t BACKLIGHT_PWM_BITS = 8;

TFT_eSPI tft;
static uint8_t brightness = 255;

void displayInit(uint8_t rotation)
{
  tft.init();
  tft.setRotation(rotation);
  tft.fillScreen(TFT_BLACK);

  ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_PWM_HZ, BACKLIGHT_PWM_BITS);
  ledcAttachPin(TFT_BL, BACKLIGHT_CHANNEL);
  displaySetBrightness(brightness);
}

void displaySetBrightness(uint8_t level)
{
  brightness = level;
  ledcWrite(BACKLIGHT_CHANNEL, level);
}

uint8_t displayGetBrightness()
{
  return brightness;
}

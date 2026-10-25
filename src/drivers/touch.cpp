#include <Arduino.h>

#include "touch.h"
#include "display.h"
#include "../board.h"

// Raw extents and orientation of the CYD panel; tune per unit if touches are offset.
static constexpr int32_t CAL_X0 = 233;
static constexpr int32_t CAL_X1 = 3785;
static constexpr int32_t CAL_Y0 = 3731;
static constexpr int32_t CAL_Y1 = 120;
static constexpr uint8_t CAL_ROTATION = 2;

static constexpr uint16_t Z_THRESHOLD = 400;
static constexpr uint8_t SAMPLES = 3;

// XPT2046 control bytes: 12-bit differential; final command powers down with PENIRQ enabled.
static constexpr uint8_t CMD_X = 0xD1;
static constexpr uint8_t CMD_Y = 0x91;
static constexpr uint8_t CMD_Z1 = 0xB1;
static constexpr uint8_t CMD_Z2 = 0xC1;
static constexpr uint8_t CMD_POWER_DOWN = 0xD0;

static void clockPulse()
{
  digitalWrite(PIN_TOUCH_CLK, HIGH);
  delayMicroseconds(1);
  digitalWrite(PIN_TOUCH_CLK, LOW);
  delayMicroseconds(1);
}

static uint16_t transfer(uint8_t cmd)
{
  for (int i = 7; i >= 0; i--)
  {
    digitalWrite(PIN_TOUCH_MOSI, (cmd >> i) & 1);
    clockPulse();
  }
  digitalWrite(PIN_TOUCH_MOSI, LOW);

  uint16_t value = 0;
  for (int i = 11; i >= 0; i--)
  {
    clockPulse();
    value |= digitalRead(PIN_TOUCH_MISO) << i;
  }
  return value;
}

static uint16_t sample(uint8_t cmd)
{
  transfer(cmd); // first conversion after switching axis is unsettled
  uint32_t sum = 0;
  for (uint8_t i = 0; i < SAMPLES; i++)
    sum += transfer(cmd);
  return sum / SAMPLES;
}

void touchInit()
{
  pinMode(PIN_TOUCH_CS, OUTPUT);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  pinMode(PIN_TOUCH_CLK, OUTPUT);
  digitalWrite(PIN_TOUCH_CLK, LOW);
  pinMode(PIN_TOUCH_MOSI, OUTPUT);
  pinMode(PIN_TOUCH_MISO, INPUT);
  pinMode(PIN_TOUCH_IRQ, INPUT);
}

bool touchReadRaw(uint16_t &x, uint16_t &y, uint16_t &z)
{
  digitalWrite(PIN_TOUCH_CS, LOW);

  int32_t z1 = transfer(CMD_Z1);
  int32_t z2 = transfer(CMD_Z2);
  int32_t pressure = z1 + 4095 - z2;
  z = pressure < 0 ? 0 : pressure;

  bool pressed = z >= Z_THRESHOLD;
  if (pressed)
  {
    x = sample(CMD_X);
    y = sample(CMD_Y);
  }

  transfer(CMD_POWER_DOWN);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  return pressed;
}

bool touchRead(TouchPoint &p)
{
  uint16_t rx, ry, rz;
  if (!touchReadRaw(rx, ry, rz))
    return false;

  const int32_t xMax = tft.width() - 1;
  const int32_t yMax = tft.height() - 1;
  const int32_t dx = CAL_X1 - CAL_X0;
  const int32_t dy = CAL_Y1 - CAL_Y0;
  const int32_t nx = rx - CAL_X0;
  const int32_t ny = ry - CAL_Y0;

  int32_t sx, sy;
  switch ((4 + (tft.getRotation() & 3) - CAL_ROTATION) % 4)
  {
  case 0:
    sx = nx * xMax / dx;
    sy = ny * yMax / dy;
    break;
  case 1:
    sx = ny * xMax / dy;
    sy = yMax - nx * yMax / dx;
    break;
  case 2:
    sx = xMax - nx * xMax / dx;
    sy = yMax - ny * yMax / dy;
    break;
  default:
    sx = xMax - ny * xMax / dy;
    sy = nx * yMax / dx;
    break;
  }

  p.x = constrain(sx, 0, xMax);
  p.y = constrain(sy, 0, yMax);
  p.z = rz;
  return true;
}

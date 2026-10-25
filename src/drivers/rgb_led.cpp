#include <Arduino.h>

#include "rgb_led.h"
#include "../board.h"

void rgbLedInit()
{
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  rgbLedSet(false, false, false);
}

void rgbLedSet(bool red, bool green, bool blue)
{
  digitalWrite(PIN_LED_R, !red);
  digitalWrite(PIN_LED_G, !green);
  digitalWrite(PIN_LED_B, !blue);
}

#pragma once

// ESP32-2432S028 ("Cheap Yellow Display") pin map.
// Display (ILI9341) pins are passed to TFT_eSPI via build flags in platformio.ini.

#define PIN_BUTTON_BOOT 0

// RGB LED, active LOW
#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17

#define PIN_LDR     34
#define PIN_SPEAKER 26

// XPT2046 resistive touch controller (own pins, driven by software SPI)
#define PIN_TOUCH_CS   33
#define PIN_TOUCH_CLK  25
#define PIN_TOUCH_MOSI 32
#define PIN_TOUCH_MISO 39
#define PIN_TOUCH_IRQ  36

// microSD slot (VSPI)
#define PIN_SD_CS   5
#define PIN_SD_SCK  18
#define PIN_SD_MOSI 23
#define PIN_SD_MISO 19

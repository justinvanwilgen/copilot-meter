#include <SPI.h>

#include "sdcard.h"
#include "../board.h"

bool sdcardInit()
{
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, SPI))
    return false;
  return SD.cardType() != CARD_NONE;
}

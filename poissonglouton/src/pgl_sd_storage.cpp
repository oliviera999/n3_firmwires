#include "pgl_sd_storage.h"

#include "config.h"
#include "pgl_log.h"

#if PGL_HEADLESS

bool pglSdStorageBegin() { return false; }
bool pglSdStorageIsMounted() { return false; }

#else

#include <SD.h>
#include <SPI.h>

namespace {
SPIClass spiSD(HSPI);
bool mounted_ = false;
}  // namespace

bool pglSdStorageBegin() {
  if (mounted_) return true;

  PGL_LOG_V("SD: init SPI (CS=%d SCK=%d MOSI=%d MISO=%d)...",
            PGL_SD_CS, PGL_SD_SCK, PGL_SD_MOSI, PGL_SD_MISO);
  spiSD.begin(PGL_SD_SCK, PGL_SD_MISO, PGL_SD_MOSI, PGL_SD_CS);
  mounted_ = SD.begin(PGL_SD_CS, spiSD, 10000000);
  if (!mounted_) {
    PGL_LOG("SD: montage echoue");
    return false;
  }
  PGL_LOG("SD: montee OK");
  return true;
}

bool pglSdStorageIsMounted() {
  return mounted_ && SD.cardType() != CARD_NONE;
}

#endif

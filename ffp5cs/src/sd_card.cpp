/**
 * @file sd_card.cpp
 * @brief Implémentation module carte SD (SPI) pour ESP32-S3.
 *
 * Broches : CS=10, MOSI=11, CLK=12, MISO=14 (pins.h, BOARD_S3 uniquement).
 * Montage sur "/sdcard". Test minimal après init (totalBytes/usedBytes).
 */

#include "sd_card.h"
#include "boot_log.h"

#if defined(BOARD_S3)

#include "pins.h"
#include <SD.h>
#include <SPI.h>

namespace {

constexpr uint32_t SD_SPI_FREQ_HZ = 4000000;
constexpr const char* SD_MOUNTPOINT = "/sdcard";

static bool s_present = false;

/** Test minimal : lecture capacité pour vérifier que la carte répond. */
static bool runTest() {
  if (!SD.exists("/")) {
    return false;
  }
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  (void)total;
  (void)used;
  return true;
}

}  // namespace

namespace SdCard {

bool init() {
  if (s_present) {
    return true;
  }
  SPI.begin(Pins::SD_CLK_PIN, Pins::SD_MISO_PIN, Pins::SD_MOSI_PIN, Pins::SD_CS_PIN);
  if (!SD.begin(static_cast<uint8_t>(Pins::SD_CS_PIN), SPI, SD_SPI_FREQ_HZ, SD_MOUNTPOINT)) {
    BOOT_LOG("[SD] Carte non montée\n");
    return false;
  }
  if (!runTest()) {
    BOOT_LOG("[SD] Test échoué après montage\n");
    SD.end();
    return false;
  }
  s_present = true;
  return true;
}

bool isPresent() {
  return s_present;
}

}  // namespace SdCard

#else  // !BOARD_S3

namespace SdCard {

bool init() {
  return false;
}

bool isPresent() {
  return false;
}

}  // namespace SdCard

#endif  // BOARD_S3

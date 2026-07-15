#include "camera_setup.h"

#include <Arduino.h>
#include "esp_camera.h"
#include <esp_heap_caps.h>
#include <esp_chip_info.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION_MAJOR >= 5
#include <esp_psram.h>
#endif
#include <cstring>
#include <Wire.h>

#include "n3_log.h"

static constexpr int kCamXclkLedcChannel = 0;

static void n3CamXclkOn(void) {
  if (XCLK_GPIO_NUM < 0) {
    return;
  }
#if ESP_IDF_VERSION_MAJOR >= 5
  ledcAttach(XCLK_GPIO_NUM, CAM_XCLK_HZ, 1);
#else
  ledcSetup(kCamXclkLedcChannel, CAM_XCLK_HZ, 1);
  ledcAttachPin(XCLK_GPIO_NUM, kCamXclkLedcChannel);
#endif
}

static void n3CamXclkOff(void) {
  if (XCLK_GPIO_NUM < 0) {
    return;
  }
#if ESP_IDF_VERSION_MAJOR >= 5
  ledcDetach(XCLK_GPIO_NUM);
#else
  ledcDetachPin(XCLK_GPIO_NUM);
#endif
}

static bool n3PsramDriverInitialized(void) {
#if ESP_IDF_VERSION_MAJOR >= 5
  return esp_psram_is_initialized();
#else
  return psramFound() || heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
#endif
}

static size_t n3PsramChipSizeBytes(void) {
#if ESP_IDF_VERSION_MAJOR >= 5
  return esp_psram_is_initialized() ? esp_psram_get_size() : 0;
#else
  return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
#endif
}

/* SPIRAM : board esp32dev + dio_qspi → CONFIG_SPIRAM=y mais psramFound() souvent faux ;
 * le tas IDF (heap_caps MALLOC_CAP_SPIRAM) est la source de vérité runtime. */
bool n3CameraSpiramHeapPresent(void) {
  return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
}

static bool n3CameraSpiramLooksViableForSxga() {
  const size_t largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const size_t total = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  return (largest >= CAM_SPIRAM_MIN_LARGEST_BLOCK && total >= CAM_SPIRAM_MIN_FREE_BYTES);
}

bool n3CameraUseHighResBuffers() {
  return n3CameraSpiramHeapPresent() && n3CameraSpiramLooksViableForSxga();
}

struct CameraInitPlan {
  framesize_t frame_size;
  int fb_count;
  camera_fb_location_t fb_location;
  camera_grab_mode_t grab_mode;
  const char* label;
};

/* Cycle PWDN (+ RESET si câblé) : réveille l'OV2640 après deep sleep ou init partielle. */
static void n3CameraHardwareReset(bool withXclk) {
  if (RESET_GPIO_NUM >= 0) {
    pinMode(RESET_GPIO_NUM, OUTPUT);
    digitalWrite(RESET_GPIO_NUM, LOW);
    delay(10);
    digitalWrite(RESET_GPIO_NUM, HIGH);
    delay(10);
  }

  if (PWDN_GPIO_NUM >= 0) {
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH);
    delay(CAM_PWDN_POWERDOWN_MS);
    digitalWrite(PWDN_GPIO_NUM, LOW);
    delay(CAM_PWDN_WAKEUP_MS);
  }

  if (withXclk && XCLK_GPIO_NUM >= 0) {
    n3CamXclkOn();
    delay(CAM_XCLK_SETTLE_MS);
  }
}

static void n3CameraReleaseProbeBus(void) {
  Wire.end();
  n3CamXclkOff();
}

static esp_err_t n3TryCameraInit(camera_config_t& config, const CameraInitPlan& plan) {
  config.frame_size = plan.frame_size;
  config.fb_count = plan.fb_count;
  config.fb_location = plan.fb_location;
  config.grab_mode = plan.grab_mode;
  config.jpeg_quality = 4;

  const size_t spiramTotal = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t spiramLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const size_t intFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  N3_LOGI("[CAM] init tentative: %s fb_count=%d loc=%s "
          "(spiram_free=%lu largest=%lu dram_free=%lu)",
          plan.label,
          plan.fb_count,
          plan.fb_location == CAMERA_FB_IN_PSRAM ? "PSRAM" : "DRAM",
          static_cast<unsigned long>(spiramTotal),
          static_cast<unsigned long>(spiramLargest),
          static_cast<unsigned long>(intFree));
  return esp_camera_init(&config);
}

esp_err_t n3CameraInitWithFallback(camera_config_t* config, char* activeModeLabel,
                                   size_t activeModeLabelLen) {
  if (!config || !activeModeLabel || activeModeLabelLen == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  activeModeLabel[0] = '\0';

  static const CameraInitPlan kPlans[] = {
      {FRAMESIZE_SXGA, 2, CAMERA_FB_IN_PSRAM, CAMERA_GRAB_LATEST, "SXGA/psram"},
      {FRAMESIZE_CIF, 1, CAMERA_FB_IN_PSRAM, CAMERA_GRAB_WHEN_EMPTY, "CIF/psram"},
      {FRAMESIZE_SVGA, 1, CAMERA_FB_IN_DRAM, CAMERA_GRAB_WHEN_EMPTY, "SVGA/dram"},
      {FRAMESIZE_CIF, 1, CAMERA_FB_IN_DRAM, CAMERA_GRAB_WHEN_EMPTY, "CIF/dram"},
      {FRAMESIZE_QQVGA, 1, CAMERA_FB_IN_DRAM, CAMERA_GRAB_WHEN_EMPTY, "QQVGA/dram"},
  };

  size_t startIdx = 0;
  if (!n3CameraSpiramHeapPresent()) {
    /* SVGA/DRAM exige un bloc DMA 32 Ko contigu — rare sans PSRAM après WiFi/SD. */
    startIdx = 3;
    N3_LOGW("[CAM] Tas SPIRAM=0 : saut PSRAM + SVGA/DRAM, repli CIF/DRAM "
            "(cf. [DIAG] build vs materiel).");
  } else if (!n3CameraSpiramLooksViableForSxga()) {
    startIdx = 1;
    N3_LOGW("[CAM] SPIRAM insuffisante pour SXGA : saut direct CIF/psram.");
  }

  n3CameraHardwareReset(false);

  for (size_t i = startIdx; i < (sizeof(kPlans) / sizeof(kPlans[0])); ++i) {
    const esp_err_t err = n3TryCameraInit(*config, kPlans[i]);
    if (err == ESP_OK) {
      snprintf(activeModeLabel, activeModeLabelLen, "%s", kPlans[i].label);
      return ESP_OK;
    }
    N3_LOGW("[CAM] Echec %s (0x%x)", kPlans[i].label, static_cast<unsigned>(err));
    esp_camera_deinit();
    delay(CAM_DEINIT_SETTLE_MS);
    n3CameraHardwareReset(false);
  }
  return ESP_FAIL;
}

/* Diagnostic serie : interpreter PSRAM / flash / puce (connectique, variante module, sdkconfig). */
void n3LogHardwareDiagnostics() {
  esp_chip_info_t ci = {};
  esp_chip_info(&ci);
  const uint32_t flashSz = ESP.getFlashChipSize();
  const size_t spiramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  const size_t spiramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t spiramLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const uint32_t intTotal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t intFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  N3_LOGD("[DIAG] --- materiel (uploadphotosserver) ---");
  N3_LOGD("[DIAG] chip_model=%s cores=%d revision=%d features=0x%08lx",
          ESP.getChipModel(),
          ci.cores,
          ci.revision,
          static_cast<unsigned long>(ci.features));
  N3_LOGD("[DIAG] flash_chip_size=%lu bytes", static_cast<unsigned long>(flashSz));
  N3_LOGD("[DIAG] ram_internal total=%lu free=%lu min_free_heap=%lu",
          static_cast<unsigned long>(intTotal),
          static_cast<unsigned long>(intFree),
          static_cast<unsigned long>(ESP.getMinFreeHeap()));
  N3_LOGD("[DIAG] spiram_heap total=%lu free=%lu largest_block=%lu bytes",
          static_cast<unsigned long>(spiramTotal),
          static_cast<unsigned long>(spiramFree),
          static_cast<unsigned long>(spiramLargest));
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
  N3_LOGD("[DIAG] build sdkconfig: CONFIG_SPIRAM=y");
#else
  N3_LOGD("[DIAG] build sdkconfig: CONFIG_SPIRAM=n (puce absente ou non detectee sur ce module)");
#endif
  const bool psramDriverInit = n3PsramDriverInitialized();
  const size_t psramChipBytes = n3PsramChipSizeBytes();
  N3_LOGD("[DIAG] esp_psram_is_initialized=%s chip_size=%lu bytes",
          psramDriverInit ? "true" : "false",
          static_cast<unsigned long>(psramChipBytes));
  N3_LOGD("[DIAG] psramFound()=%s  SXGA_seuils total>=%u largest>=%u",
          psramFound() ? "true" : "false",
          static_cast<unsigned>(CAM_SPIRAM_MIN_FREE_BYTES),
          static_cast<unsigned>(CAM_SPIRAM_MIN_LARGEST_BLOCK));
  if (spiramTotal == 0) {
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    if (psramChipBytes == 0) {
      N3_LOGW("[DIAG] Build SPIRAM active mais puce absente ou non detectee "
              "(module clone sans PSRAM, soudure, alim) — mode DRAM prevu.");
    } else {
      N3_LOGW("[DIAG] Puce PSRAM detectee mais tas heap SPIRAM=0 "
              "(init heap ou fragmentation anormale).");
    }
#else
    N3_LOGW("[DIAG] SPIRAM tas=0 : module sans PSRAM ou puce non detectee.");
#endif
  } else if (spiramLargest < CAM_SPIRAM_MIN_LARGEST_BLOCK) {
    N3_LOGW("[DIAG] Plus grand bloc SPIRAM < seuil SXGA : fragmentation, PSRAM "
            "partielle, ou charge memoire avant camera.");
  } else if (spiramFree < CAM_SPIRAM_MIN_FREE_BYTES) {
    N3_LOGW("[DIAG] SPIRAM libre < seuil total SXGA.");
  } else {
    N3_LOGD("[DIAG] Criteres quantitatifs SPIRAM OK pour tenter SXGA (init peut encore "
            "echouer : nappe OV2640, alim, timing).");
  }
  N3_LOGD("[DIAG] --------------------------------------");
}

static int sccbEndTransmission(uint8_t addr7) {
  Wire.beginTransmission(addr7);
  return Wire.endTransmission();
}

static bool sccbReadReg8(uint8_t addr7, uint8_t reg, uint8_t* out) {
  Wire.beginTransmission(addr7);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr7, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  *out = Wire.read();
  return true;
}

static const char* sccbNackHint(int err) {
  switch (err) {
    case 0:
      return "ACK";
    case 1:
      return "buffer trop long";
    case 2:
      return "NACK adresse (peripherique absent ou bus coupe)";
    case 3:
      return "NACK donnees";
    case 4:
      return "autre erreur";
    case 5:
      return "timeout";
    default:
      return "inconnu";
  }
}

static int n3SccbPingWithRetries(uint8_t addr7, bool* usedSlowClock) {
  if (usedSlowClock) {
    *usedSlowClock = false;
  }

  int lastErr = 2;
  for (int attempt = 0; attempt < CAM_SCCB_RETRY_COUNT; ++attempt) {
    const uint32_t clockHz =
        (attempt == CAM_SCCB_RETRY_COUNT - 1) ? CAM_SCCB_CLOCK_SLOW_HZ : CAM_SCCB_CLOCK_HZ;
    if (attempt == CAM_SCCB_RETRY_COUNT - 1 && usedSlowClock) {
      *usedSlowClock = true;
    }
    Wire.setClock(clockHz);
    delay(CAM_SCCB_RETRY_BASE_MS * static_cast<uint32_t>(attempt + 1));
    lastErr = sccbEndTransmission(addr7);
    if (lastErr == 0) {
      if (attempt == 0) {
        N3_LOGD("[DIAG][SCCB] ping 0x%02X (OV2640 AI-Thinker) -> ACK (1/%d)",
                addr7,
                CAM_SCCB_RETRY_COUNT);
      } else {
        N3_LOGD("[DIAG][SCCB] ping 0x%02X OK a la tentative %d/%d (%lu Hz)",
                addr7,
                attempt + 1,
                CAM_SCCB_RETRY_COUNT,
                static_cast<unsigned long>(clockHz));
      }
      return 0;
    }
    N3_LOGD("[DIAG][SCCB] ping 0x%02X tentative %d/%d -> %s (%d)",
            addr7,
            attempt + 1,
            CAM_SCCB_RETRY_COUNT,
            sccbNackHint(lastErr),
            lastErr);
  }
  return lastErr;
}

void n3LogCameraSccbDiagnostics(void) {
  static constexpr uint8_t kOv2640Addr = 0x30;
  static constexpr uint8_t kRegPidHigh = 0x0A;
  static constexpr uint8_t kRegPidLow = 0x0B;

  N3_LOGD("[DIAG][SCCB] --- sonde bus camera (avant esp_camera_init) ---");
  N3_LOGD("[DIAG][SCCB] broches SDA=%d SCL=%d PWDN=%d XCLK=%d RESET=%d",
          SIOD_GPIO_NUM,
          SIOC_GPIO_NUM,
          PWDN_GPIO_NUM,
          XCLK_GPIO_NUM,
          RESET_GPIO_NUM);

  n3CameraHardwareReset(true);
  if (XCLK_GPIO_NUM >= 0) {
    N3_LOGD("[DIAG][SCCB] horloge pixel XCLK=%lu Hz active sur GPIO%d (settle=%u ms)",
            static_cast<unsigned long>(CAM_XCLK_HZ),
            XCLK_GPIO_NUM,
            static_cast<unsigned>(CAM_XCLK_SETTLE_MS));
  }

  Wire.begin(SIOD_GPIO_NUM, SIOC_GPIO_NUM);
  Wire.setClock(CAM_SCCB_CLOCK_HZ);
  delay(10);

  bool slowClockUsed = false;
  const int ping = n3SccbPingWithRetries(kOv2640Addr, &slowClockUsed);

  if (ping == 0) {
    uint8_t pidHigh = 0;
    uint8_t pidLow = 0;
    const bool gotHigh = sccbReadReg8(kOv2640Addr, kRegPidHigh, &pidHigh);
    const bool gotLow = sccbReadReg8(kOv2640Addr, kRegPidLow, &pidLow);
    if (gotHigh && gotLow) {
      /* Ligne d'origine construite en plusieurs appels (printf sans \n + println du
         suffixe) : chaque macro n3_log ajoute prefixe + \n, donc on fusionne le PID
         et son interpretation en UN SEUL N3_LOGD (le suffixe passe en argument %s). */
      const char* pidHint;
      if (pidHigh == 0x26 && pidLow == 0x42) {
        pidHint = " -> OV2640 confirme";
      } else if (pidHigh == 0x56 && pidLow == 0x40) {
        pidHint = " -> capteur OV5640 (pas OV2640)";
      } else if (pidHigh == 0x21 && pidLow == 0x45) {
        pidHint = " -> capteur GC2145 (pas OV2640)";
      } else {
        pidHint = " -> identifiant inconnu";
      }
      N3_LOGD("[DIAG][SCCB] PID lu: 0x%02X%02X%s", pidHigh, pidLow, pidHint);
    } else {
      N3_LOGW("[DIAG][SCCB] ACK adresse mais lecture PID impossible "
              "(nappe, alim ou timing)");
    }
  } else {
    N3_LOGW("[DIAG][SCCB] Pas de reponse a 0x30 : verifier nappe FFC, "
            "alim 5V, contacts vers la carte");
  }

  /* Ligne d'origine construite en plusieurs appels (print du prefixe + printf par
     adresse detectee + println final) : bufferisee dans scanMsg puis emise en UN
     SEUL N3_LOGD (une macro n3_log = un prefixe + \n, incompatible avec la
     construction morceau par morceau). */
  char scanMsg[256];
  int off = snprintf(scanMsg, sizeof(scanMsg), "[DIAG][SCCB] peripheriques detectes:");
  int found = 0;
  for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
    if (sccbEndTransmission(addr) == 0) {
      if (off >= 0 && static_cast<size_t>(off) < sizeof(scanMsg)) {
        off += snprintf(scanMsg + off, sizeof(scanMsg) - static_cast<size_t>(off), " 0x%02X", addr);
      }
      ++found;
    }
  }
  if (found == 0) {
    if (off >= 0 && static_cast<size_t>(off) < sizeof(scanMsg)) {
      snprintf(scanMsg + off, sizeof(scanMsg) - static_cast<size_t>(off), " aucun");
    }
  }
  N3_LOGD("%s", scanMsg);

  n3CameraReleaseProbeBus();
  N3_LOGD("[DIAG][SCCB] ---------------------------------------------");
}

#if USE_DEEP_SLEEP
// Warm-up raccourci (A8, audit 2026-06/2026-07) : l'OV2640 se stabilise en 1-2 trames, pas 3 s.
// On jette CAM_WARMUP_FRAME_COUNT trames espacées de CAM_WARMUP_FRAME_DELAY_MS ; l'AEC MATÉRIEL de
// l'OV2640 (réactivé dans initializeCamera) gère l'exposition — pas de mesure de luminosité logicielle
// (l'ancien adjustExposure moyennait des octets JPEG compressés, mesure inopérante : B1).
// ⚠ Durées ajustables via config.h (revenir à 3×1000 ms si dégradation en aube/crépuscule).
void warmupCamera() {
  for (int i = 0; i < CAM_WARMUP_FRAME_COUNT; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);  // libération inconditionnelle (pas de fuite framebuffer, cf. M1)
    delay(CAM_WARMUP_FRAME_DELAY_MS);
  }
}

void initializeCamera() {
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_exposure_ctrl(s, 0);
    s->set_aec_value(s, 300);
    s->set_gain_ctrl(s, 0);
    s->set_agc_gain(s, 0);
    delay(CAM_AEC_PRELOCK_MS);
    // Réactivation de l'AEC/AGC/AWB MATÉRIEL : c'est lui (et non un réglage logiciel) qui pilote
    // l'exposition à chaque capture. Convergence en lumière stable ≈ CAM_AEC_SETTLE_MS.
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_awb_gain(s, 1);
    delay(CAM_AEC_SETTLE_MS);
  }
}
#endif

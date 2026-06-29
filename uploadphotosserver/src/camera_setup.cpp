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

  Serial.printf("[CAM] init tentative: %s fb_count=%d loc=%s "
                "(spiram_free=%lu largest=%lu dram_free=%lu)\n",
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
    startIdx = 2;
    Serial.println("[CAM][WARN] Tas SPIRAM=0 : saut des profils PSRAM, repli DRAM "
                   "(cf. [DIAG] build vs materiel).");
  } else if (!n3CameraSpiramLooksViableForSxga()) {
    startIdx = 1;
    Serial.println("[CAM][WARN] SPIRAM insuffisante pour SXGA : saut direct CIF/psram.");
  }

  for (size_t i = startIdx; i < (sizeof(kPlans) / sizeof(kPlans[0])); ++i) {
    const esp_err_t err = n3TryCameraInit(*config, kPlans[i]);
    if (err == ESP_OK) {
      snprintf(activeModeLabel, activeModeLabelLen, "%s", kPlans[i].label);
      return ESP_OK;
    }
    Serial.printf("[CAM][WARN] Echec %s (0x%x)\n", kPlans[i].label, static_cast<unsigned>(err));
    esp_camera_deinit();
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

  Serial.println("[DIAG] --- materiel (uploadphotosserver) ---");
  Serial.printf("[DIAG] chip_model=%s cores=%d revision=%d features=0x%08lx\n",
                ESP.getChipModel(),
                ci.cores,
                ci.revision,
                static_cast<unsigned long>(ci.features));
  Serial.printf("[DIAG] flash_chip_size=%lu bytes\n", static_cast<unsigned long>(flashSz));
  Serial.printf("[DIAG] ram_internal total=%lu free=%lu min_free_heap=%lu\n",
                static_cast<unsigned long>(intTotal),
                static_cast<unsigned long>(intFree),
                static_cast<unsigned long>(ESP.getMinFreeHeap()));
  Serial.printf("[DIAG] spiram_heap total=%lu free=%lu largest_block=%lu bytes\n",
                static_cast<unsigned long>(spiramTotal),
                static_cast<unsigned long>(spiramFree),
                static_cast<unsigned long>(spiramLargest));
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
  Serial.println("[DIAG] build sdkconfig: CONFIG_SPIRAM=y");
#else
  Serial.println("[DIAG] build sdkconfig: CONFIG_SPIRAM=n (preferez env *-cam sur module AI-Thinker)");
#endif
  const bool psramDriverInit = n3PsramDriverInitialized();
  const size_t psramChipBytes = n3PsramChipSizeBytes();
  Serial.printf("[DIAG] esp_psram_is_initialized=%s chip_size=%lu bytes\n",
                psramDriverInit ? "true" : "false",
                static_cast<unsigned long>(psramChipBytes));
  Serial.printf("[DIAG] psramFound()=%s  SXGA_seuils total>=%u largest>=%u\n",
                psramFound() ? "true" : "false",
                static_cast<unsigned>(CAM_SPIRAM_MIN_FREE_BYTES),
                static_cast<unsigned>(CAM_SPIRAM_MIN_LARGEST_BLOCK));
  if (spiramTotal == 0) {
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    if (psramChipBytes == 0) {
      Serial.println("[DIAG][WARN] Build SPIRAM active mais puce absente ou non detectee "
                     "(module clone sans PSRAM, soudure, alim) — mode DRAM prevu.");
    } else {
      Serial.println("[DIAG][WARN] Puce PSRAM detectee mais tas heap SPIRAM=0 "
                     "(init heap ou fragmentation anormale).");
    }
#else
    Serial.println("[DIAG][WARN] SPIRAM tas=0 : profil build sans SPIRAM ou module sans PSRAM.");
#endif
  } else if (spiramLargest < CAM_SPIRAM_MIN_LARGEST_BLOCK) {
    Serial.println("[DIAG][WARN] Plus grand bloc SPIRAM < seuil SXGA : fragmentation, PSRAM "
                   "partielle, ou charge memoire avant camera.");
  } else if (spiramFree < CAM_SPIRAM_MIN_FREE_BYTES) {
    Serial.println("[DIAG][WARN] SPIRAM libre < seuil total SXGA.");
  } else {
    Serial.println("[DIAG] Criteres quantitatifs SPIRAM OK pour tenter SXGA (init peut encore "
                   "echouer : nappe OV2640, alim, timing).");
  }
  Serial.println("[DIAG] --------------------------------------");
}

#if USE_DEEP_SLEEP
void adjustExposure() {
  sensor_t* s = esp_camera_sensor_get();
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb && s) {
    long brightness = 0;
    for (size_t i = 0; i < fb->len; i++) brightness += fb->buf[i];
    brightness /= (fb->len ? (long)fb->len : 1);
    const int currentAec = s->status.aec_value;
    if (brightness > 200) {
      int nextAec = currentAec - 50;
      if (nextAec < 0) nextAec = 0;
      s->set_aec_value(s, nextAec);
    } else if (brightness < 50) {
      int nextAec = currentAec + 50;
      if (nextAec > 1200) nextAec = 1200;
      s->set_aec_value(s, nextAec);
    }
    esp_camera_fb_return(fb);
  }
}

// Warm-up raccourci (audit algo 2026-06) : l'OV2640 se stabilise en 1-2
// trames, pas 3 s. 2 trames jetees a 150 ms suffisent ; adjustExposure()
// corrige ensuite avant chaque capture. ⚠ A valider sur cible en transitions
// de luminosite (aube/crepuscule) : revenir a 3×1000 ms si photos degradees.
void warmupCamera() {
  for (int i = 0; i < 2; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(150);
  }
}

void initializeCamera() {
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_exposure_ctrl(s, 0);
    s->set_aec_value(s, 300);
    s->set_gain_ctrl(s, 0);
    s->set_agc_gain(s, 0);
    delay(500);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_awb_gain(s, 1);
    // 10 s -> 3 s : la convergence AEC/AWB de l'OV2640 prend 2-3 s en
    // lumiere stable (10 s etait une marge excessive).
    delay(3000);
  }
}
#endif

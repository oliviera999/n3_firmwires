#include "camera_setup.h"

#include <Arduino.h>
#include "esp_camera.h"
#include <esp_heap_caps.h>
#include <esp_chip_info.h>

/* SPIRAM : board esp32dev → psramFound() souvent faux malgré tas IDF. Il faut assez de SPIRAM
 * libre et un bloc contigu suffisant (sinon cam_hal: frame buffer malloc failed en SXGA). */
static bool n3CameraSpiramLooksViableForSxga() {
  const size_t largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  const size_t total = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  return (largest >= CAM_SPIRAM_MIN_LARGEST_BLOCK && total >= CAM_SPIRAM_MIN_FREE_BYTES);
}

bool n3CameraUseHighResBuffers() {
  if (psramFound()) {
    return n3CameraSpiramLooksViableForSxga();
  }
  return n3CameraSpiramLooksViableForSxga();
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
  Serial.printf("[DIAG] psramFound()=%s  SXGA_seuils total>=%u largest>=%u\n",
                psramFound() ? "true" : "false",
                static_cast<unsigned>(CAM_SPIRAM_MIN_FREE_BYTES),
                static_cast<unsigned>(CAM_SPIRAM_MIN_LARGEST_BLOCK));
  if (spiramTotal == 0) {
    Serial.println("[DIAG][WARN] SPIRAM tas=0 : variante sans puce PSRAM, soudure, alim, ou profil "
                   "build sans SPIRAM (verifier memory_type dio_qspi).");
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

void warmupCamera() {
  for (int i = 0; i < 3; i++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(1000);
  }
}

void initializeCamera() {
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_exposure_ctrl(s, 0);
    s->set_aec_value(s, 300);
    s->set_gain_ctrl(s, 0);
    s->set_agc_gain(s, 0);
    delay(1000);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_awb_gain(s, 1);
    delay(10000);
  }
}
#endif

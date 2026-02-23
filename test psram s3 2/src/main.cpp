/*
 * Projet minimal ESP32-S3 N16R8 — Boot + PSRAM OPI + stabilité
 * Teste la détection et l'utilisation de la PSRAM 8 Mo, nourrit le TWDT, affiche les stats heap.
 * L'IWDT (TG1) est désactivé au tout début de setup() si le bootloader l'a laissé actif (300 ms).
 */

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "hal/wdt_hal.h"
#include "hal/wdt_types.h"

static const uint32_t SERIAL_BAUD = 115200;
static const uint32_t WDT_TIMEOUT_S = 300;
static const uint32_t STATS_INTERVAL_MS = 10000;
static const size_t PSRAM_TEST_SIZE = 1024 * 1024;  // 1 Mo
static const uint8_t PATTERN_A = 0xAA;
static const uint8_t PATTERN_B = 0x55;

static uint32_t s_loopCount = 0;
static uint32_t s_lastStatsMs = 0;

static bool testPsramAllocWriteRead(size_t size) {
  vTaskDelay(pdMS_TO_TICKS(50));  // laisser IWDT se nourrir avant alloc
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (!p) {
    Serial.printf("[PSRAM] alloc %u bytes FAILED\n", (unsigned)size);
    return false;
  }
  uint8_t* buf = (uint8_t*)p;
  const size_t chunk = 4096;   // 4 Ko par chunk, delay(1) entre chaque pour éviter IWDT 300 ms
  for (size_t off = 0; off < size; off += chunk) {
    size_t n = (off + chunk <= size) ? chunk : (size - off);
    for (size_t i = 0; i < n; i++) buf[off + i] = ((off + i) & 1) ? PATTERN_B : PATTERN_A;
    vTaskDelay(pdMS_TO_TICKS(20));  // laisser idle task nourrir IWDT
  }
  bool ok = true;
  for (size_t off = 0; off < size && ok; off += chunk) {
    size_t n = (off + chunk <= size) ? chunk : (size - off);
    for (size_t i = 0; i < n; i++) {
      if (buf[off + i] != (((off + i) & 1) ? PATTERN_B : PATTERN_A)) { ok = false; break; }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  heap_caps_free(p);
  return ok;
}

void setup() {
  // Désactiver l'IWDT (Timer Group 1) au plus tôt — le bootloader peut l'avoir activé à 300 ms
  {
    wdt_hal_context_t iwdt_hal = {};
    wdt_hal_init(&iwdt_hal, WDT_MWDT1, 40000, false);  // désactive MWDT1 et ses stages
    wdt_hal_write_protect_disable(&iwdt_hal);
    wdt_hal_disable(&iwdt_hal);
    wdt_hal_write_protect_enable(&iwdt_hal);
  }

  Serial.begin(SERIAL_BAUD);
  delay(500);
  Serial.println("\n--- ESP32-S3 N16R8 PSRAM test ---");

  Serial.printf("Chip: %s rev %u\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("Flash: %u Mo, %u MHz\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)), (unsigned)ESP.getFlashChipSpeed() / 1000000);
  Serial.printf("CPU freq: %u MHz\n", (unsigned)(getCpuFrequencyMhz()));

  // TWDT non démarré au boot (sdkconfig) — on le démarre ici
  esp_task_wdt_deinit();
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  esp_task_wdt_config_t wdt = {};
  wdt.timeout_ms = WDT_TIMEOUT_S * 1000;
  wdt.idle_core_mask = 0;
  wdt.trigger_panic = true;
  if (esp_task_wdt_init(&wdt) != ESP_OK) {
    Serial.println("[WDT] init failed");
  } else if (esp_task_wdt_add(NULL) != ESP_OK) {
    Serial.println("[WDT] add current task failed");
  } else {
    Serial.println("[WDT] Task WDT 300s started");
  }
#else
  if (esp_task_wdt_init(WDT_TIMEOUT_S, true) != ESP_OK) {
    Serial.println("[WDT] init failed");
  } else if (esp_task_wdt_add(NULL) != ESP_OK) {
    Serial.println("[WDT] add current task failed");
  } else {
    Serial.println("[WDT] Task WDT 300s started");
  }
#endif

  // PSRAM — détection via heap_caps (psramFound() peut être faux avec custom sdkconfig)
  size_t psramFreeCaps = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t psramSize = ESP.getPsramSize();
  size_t psramFree = ESP.getFreePsram();
  bool psramOk = (psramFreeCaps > 0) || (psramSize > 0) || psramFound();
  if (!psramOk) {
    Serial.println("[PSRAM] NOT FOUND — check qio_opi and CONFIG_SPIRAM");
    return;
  }
  Serial.printf("[PSRAM] psramFound=%d\n", psramFound() ? 1 : 0);
  Serial.printf("[PSRAM] Size: %u bytes (~%u Mo)\n", (unsigned)psramSize, (unsigned)(psramSize / (1024 * 1024)));
  Serial.printf("[PSRAM] Free (ESP): %u, Free (caps): %u\n", (unsigned)psramFree, (unsigned)psramFreeCaps);

  // Test 256 Ko puis 1 Mo (éviter blocage IWDT si bootloader a 300 ms)
  Serial.println("[PSRAM] Test 256 Ko alloc+write+read+free...");
  if (testPsramAllocWriteRead(256 * 1024)) {
    Serial.println("[PSRAM] Test 256 Ko OK");
  } else {
    Serial.println("[PSRAM] Test 256 Ko FAILED");
  }
  Serial.println("[PSRAM] Test 1 Mo alloc+write+read+free...");
  if (testPsramAllocWriteRead(PSRAM_TEST_SIZE)) {
    Serial.println("[PSRAM] Test 1 Mo OK");
  } else {
    Serial.println("[PSRAM] Test 1 Mo FAILED");
  }

  // Plusieurs alloc/free plus petites pour stabilité
  Serial.println("[PSRAM] 10 x 256 Ko alloc/free...");
  int okCount = 0;
  for (int i = 0; i < 10; i++) {
    if (testPsramAllocWriteRead(256 * 1024)) okCount++;
  }
  Serial.printf("[PSRAM] 10 tests: %d OK\n", okCount);

  Serial.printf("Heap internal free: %u, PSRAM free: %u\n",
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  Serial.println("--- setup done, entering loop ---\n");
  s_lastStatsMs = millis();
}

void loop() {
  esp_task_wdt_reset();

  uint32_t now = millis();
  if (now - s_lastStatsMs >= STATS_INTERVAL_MS) {
    s_lastStatsMs = now;
    s_loopCount++;
    Serial.printf("[%u] loops | heap internal: %u | PSRAM free: %u\n",
      (unsigned)s_loopCount,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  }

  delay(100);
}

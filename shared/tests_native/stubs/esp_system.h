#pragma once
// Stub minimal de <esp_system.h> pour les tests natifs : fournit l'enum
// esp_reset_reason_t (valeurs alignées sur ESP-IDF). Récupéré via -I stubs ;
// le build firmware utilise le vrai <esp_system.h> (stubs/ n'y est pas).

typedef enum {
  ESP_RST_UNKNOWN   = 0,
  ESP_RST_POWERON   = 1,
  ESP_RST_EXT       = 2,
  ESP_RST_SW        = 3,
  ESP_RST_PANIC     = 4,
  ESP_RST_INT_WDT   = 5,
  ESP_RST_TASK_WDT  = 6,
  ESP_RST_WDT       = 7,
  ESP_RST_DEEPSLEEP = 8,
  ESP_RST_BROWNOUT  = 9,
  ESP_RST_SDIO      = 10
} esp_reset_reason_t;

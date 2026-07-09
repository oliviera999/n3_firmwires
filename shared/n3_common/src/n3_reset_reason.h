#pragma once
// =============================================================================
// n3_common — Cause de redémarrage (libellé + classification crash)
// =============================================================================
// Mutualisé depuis ffp5cs/include/reset_reason.h (audit §3.8). Logique pure de
// mapping/classification de la cause de reset. isCrash() pilote le logging de
// crash et l'alerting : se tromper sur la liste des causes « crash » fausse les
// diagnostics post-mortem. Aucune dépendance Arduino ; en test natif l'enum
// esp_reset_reason_t vient d'un stub (stubs/esp_system.h).
// =============================================================================

#include <esp_system.h>  // esp_reset_reason_t (stub en test natif)

namespace ResetReason {

// Libellé court de la cause de reset (aligné sur les logs/diagnostics).
inline const char* resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "OTHER";
  }
}

// Vrai si la cause indique un crash (panic ou un des watchdogs).
inline bool isCrash(esp_reset_reason_t reason) {
  return (reason == ESP_RST_PANIC ||
          reason == ESP_RST_INT_WDT ||
          reason == ESP_RST_TASK_WDT ||
          reason == ESP_RST_WDT);
}

}  // namespace ResetReason

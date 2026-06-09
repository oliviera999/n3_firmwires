// task_mail.cpp — Réserve heap SMTP + traitement de la file de mails.
// Extrait d'app_tasks.cpp (refonte god-file, audit v13.93), déplacement verbatim.
#include "task_mail.h"
#include "app_context.h"   // AppContext (ctx->mailer)
#include "config.h"        // HeapConfig
#include "tls_mutex.h"     // TLSMutex::canConnect, g_enteringLightSleep
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdlib>
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif

#if FEATURE_MAIL
// Réserve 32KB pour SMTP : sur S3 avec PSRAM, allouée en PSRAM pour ne pas fragmenter la RAM interne
// (TLS/SMTP a besoin d'un bloc contigu en RAM interne ; en gardant la réserve en PSRAM on laisse 32KB dispo en interne)
static uint8_t* s_mailReserve = nullptr;
static bool s_mailReserveFromPSRAM = false;

// Alloue la réserve : PSRAM en priorité sur S3 (libère la RAM interne pour TLS), sinon malloc interne.
void allocMailReserveIfNeeded(uint32_t kMailBlockReq) {
  if (s_mailReserve) return;
#if defined(BOARD_S3) && defined(MALLOC_CAP_SPIRAM)
  size_t lbSpiral = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  if (lbSpiral >= kMailBlockReq) {
    s_mailReserve = (uint8_t*)heap_caps_malloc(kMailBlockReq, MALLOC_CAP_SPIRAM);
    if (s_mailReserve) {
      s_mailReserveFromPSRAM = true;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[Mail] Réserve 32KB PSRAM SMTP (S3)\n");
#else
      Serial.println(F("[Mail] Réserve 32KB allouée en PSRAM pour SMTP (S3)"));
#endif
      return;
    }
  }
#endif
  uint32_t lbInternal = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  if (lbInternal >= kMailBlockReq) {
    s_mailReserve = (uint8_t*)malloc(kMailBlockReq);
    if (s_mailReserve) {
      s_mailReserveFromPSRAM = false;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[Mail] Réserve 32KB boot SMTP\n");
#else
      Serial.println(F("[Mail] Réserve 32KB allouée au boot pour SMTP"));
#endif
    }
  }
}

// Traitement séquentiel des mails en attente (extrait de automationTask pour lisibilité).
void processMailQueueIfReady(AppContext* ctx, unsigned long now) {
  if (!ctx) return;
  const uint32_t kMailBlockReq = HeapConfig::MIN_HEAP_BLOCK_FOR_MAIL_TLS;
  const uint32_t kMailFreeReq = HeapConfig::MIN_HEAP_MAIL_SEND;
  if (!ctx->mailer.hasPendingMails()) {
    if (s_mailReserve) {
      free(s_mailReserve);
      s_mailReserve = nullptr;
      s_mailReserveFromPSRAM = false;
    }
    return;
  }
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  uint32_t pending = ctx->mailer.getQueuedMails();

  // Stratégie anti-fragmentation : si la réserve 31K est déjà allouée (boot), on crée le bloc contigu en la libérant.
  // Testée AVANT canConnect() : après libération on aura freeHeap+31K, donc assez pour TLS même si freeHeap actuel < 35K.
  if (!g_enteringLightSleep && s_mailReserve && !s_mailReserveFromPSRAM &&
      freeHeap >= HeapConfig::MIN_HEAP_FREE_WHEN_USING_MAIL_RESERVE) {
    free(s_mailReserve);
    s_mailReserve = nullptr;
    esp_task_wdt_reset();
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[Mail] ENVOI (reserve liberee) heap=%u\n", (unsigned)ESP.getFreeHeap());
#else
    Serial.printf("[Mail] >>> ENVOI (réserve libérée, bloc 31K dispo) - heap free=%u <<<\n", (unsigned)ESP.getFreeHeap());
#endif
    ctx->mailer.processOneMailSync();
    allocMailReserveIfNeeded(kMailBlockReq);  // Re-capturer le bloc contigu pour le prochain mail
    return;
  }

  bool canConn = TLSMutex::canConnect();
  if (!canConn) {
    static unsigned long lastTlsSkipMs = 0;
    if (now - lastTlsSkipMs > 30000) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[Mail] skip TLS pending=%u heap=%u\n", (unsigned)pending, (unsigned)freeHeap);
#else
      Serial.printf("[Mail] ⏸️ En attente: %u mail(s), skip TLS (heap=%u < 35K ou sleep)\n", pending, freeHeap);
#endif
      lastTlsSkipMs = now;
    }
    return;
  }
  // Garde heap libre (Core 1): éviter abort() si TLS/allocs internes échouent pendant processOneMailSync
  if (largestBlock >= kMailBlockReq && freeHeap >= kMailFreeReq) {
    allocMailReserveIfNeeded(kMailBlockReq);
    // Libérer la réserve AVANT l'envoi seulement si elle est en RAM interne, pour créer un bloc 32KB pour TLS.
    if (s_mailReserve && !s_mailReserveFromPSRAM) {
      free(s_mailReserve);
      s_mailReserve = nullptr;
    }
    esp_task_wdt_reset();
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[Mail] ENVOI heap=%u bloc=%u\n", (unsigned)freeHeap, (unsigned)largestBlock);
#else
    Serial.printf("[Mail] >>> ENVOI EFFECTIF (tentative) - heap free=%u bloc=%u <<<\n", freeHeap, largestBlock);
#endif
    ctx->mailer.processOneMailSync();
    allocMailReserveIfNeeded(kMailBlockReq);  // Re-capturer le bloc contigu pour le prochain mail
    return;
  }
  if (s_mailReserve && !s_mailReserveFromPSRAM) {
    free(s_mailReserve);
    s_mailReserve = nullptr;
    vTaskDelay(pdMS_TO_TICKS(100));
    largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    freeHeap = ESP.getFreeHeap();
    if (largestBlock >= kMailBlockReq && freeHeap >= kMailFreeReq) {
      esp_task_wdt_reset();
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[Mail] ENVOI apres liberation heap=%u\n", (unsigned)freeHeap);
#else
      Serial.printf("[Mail] >>> ENVOI EFFECTIF (tentative après libération réserve) - heap free=%u <<<\n", freeHeap);
#endif
      ctx->mailer.processOneMailSync();
      allocMailReserveIfNeeded(kMailBlockReq);  // Re-capturer le bloc contigu pour le prochain mail
    }
    return;
  }
  // Ré-allocation périodique : sans réserve (déjà utilisée pour un envoi), on retente toutes les 30 s
  // de capturer un bloc 31K pour débloquer les mails suivants (fragmentation peut libérer un bloc plus tard).
  static unsigned long lastReserveRetryMs = 0;
  if (!s_mailReserve && pending > 0 && (now - lastReserveRetryMs) >= 30000) {
    lastReserveRetryMs = now;
    allocMailReserveIfNeeded(kMailBlockReq);
    if (s_mailReserve) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
      ets_printf("[Mail] Reserve re-allouee (prochain envoi possible)\n");
#else
      Serial.println(F("[Mail] ✅ Réserve re-allouée (prochain envoi possible)"));
#endif
    }
  }
  static unsigned long lastMemWarnMs = 0;
  if (now - lastMemWarnMs > 60000) {
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[Mail] skip heap pending=%u blk=%u\n", (unsigned)pending, (unsigned)largestBlock);
#else
    Serial.printf("[Mail] ⏸️ En attente: %u mail(s), skip heap (bloc=%u < %uK requis, free=%u)\n",
                  pending, largestBlock, kMailBlockReq / 1024, freeHeap);
#endif
    lastMemWarnMs = now;
  }
}

// Libère la réserve seulement si en RAM interne (appelé par prepareOtaExclusiveHeap).
// Retourne true si une libération a eu lieu.
bool mailReserveReleaseIfInternal() {
  if (s_mailReserve && !s_mailReserveFromPSRAM) {
    free(s_mailReserve);
    s_mailReserve = nullptr;
    return true;
  }
  return false;
}
#endif // FEATURE_MAIL

#pragma once
// =============================================================================
// n3_data — Diagnostic de perte de contact mutualisé (n3pp, msp, …) — n3_data 1.4
// =============================================================================
// Portage pour les firmwares à deep sleep de ce que ffp5cs 15.34 fait en light sleep
// (ffp5cs/include/contact_diag.h), après l'incident FFP3 du 06/11/2026
// (n3_serveur docs/DIAGNOSTIC_FFP3_PERTE_CONTACT_2026-11-06.md) :
//   1. diagnostic de boot : cause de reset, partition, état OTA de l'image, compteur de réveils
//      consécutifs sans contact sain (→ mode sans échec = OTA suspendue, cf. n3_ota_hooks.h) ;
//   3. événements OTA (check / start / progress / done / fail / paused) via les accroches de
//      n3_ota, persistés en NVS AVANT le reboot et renvoyés jusqu'à acquittement (/ota-report) ;
//   4. journal de bord persistant (anneau n3evt::Ring en NVS « n3evt ») : Wi-Fi, NTP, codes HTTP,
//      entrée en veille, boot/réveil ;
//   5. horloge : décalage module − serveur mesuré sur X-Server-Ts à chaque réponse, repli sur
//      l'heure serveur quand NTP a échoué (n3TimeApplyServerEpoch), champ `clk` du heartbeat.
// Un contact est SAIN quand un POST signé (X-Sig) est accepté : le compteur de réveils ratés
// revient à zéro. Logique pure : n3_common/n3_event_ring.h, n3_common/n3_boot_diag.h,
// n3_time/n3_sleep_clock.h (tests natifs shared/tests_native).
// Aucun secret ne transite : les champs ajoutés sont diagnostiques (contrats n3pp.json / msp1.json).
// =============================================================================
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "n3_data.h"

struct N3ContactDiagConfig {
  const char* firmwareVersion;   // FIRMWARE_VERSION
  const char* board;             // libellé carte : X-Ota-Board + champ `board` (ex. "n3pp", "msp1")
  const char* source;            // champ `src` de /ota-report : source serveur, "ota_report" (DeviceEventRepository)
  const char* otaReportUrl;      // POST /ota-report (cf. n3ContactDiagDeriveOtaReportUrl)
  const char* apiKeyHeader;      // X-Api-Key (API_KEY) — NULL = aucun
  const char* apiKeyField;       // champ `api_key`
  const char* sigSecret;         // API_SIG_SECRET : OBLIGATOIRE pour /ota-report (HMAC requis côté serveur)
  unsigned long (*epochProvider)();  // epoch pour signer, 0 = horloge non fiable
  // Repli d'horloge sans NTP : applique l'heure serveur (retourne true si l'horloge a bougé).
  bool (*applyServerEpoch)(uint32_t serverEpoch, uint32_t rttMs);  // NULL = jamais appliqué
};

/// "https://host/n3pp/heartbeat" → "https://host/n3pp/ota-report" (même suffixe d'environnement,
/// cf. n3_serveur config/routes_helpers.php). Retourne false si l'URL ne finit pas par /heartbeat.
bool n3ContactDiagDeriveOtaReportUrl(const char* heartbeatUrl, char* out, size_t outSize);

/// À appeler tôt dans setup() (après Serial, avant Wi-Fi) : charge le journal, incrémente et
/// persiste le compteur de réveils ratés, journalise `boot` (reset) ou `wake` (deep sleep),
/// enregistre l'observateur de réponses n3_data.
void n3ContactDiagBegin(const N3ContactDiagConfig& cfg);

void n3ContactDiagLog(const char* event, const char* detail = nullptr);
void n3ContactDiagLogf(const char* event, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

/// Résultat de la connexion Wi-Fi (journalise wifi_ok / wifi_fail, mémorise durée et essais).
void n3ContactDiagOnWifiResult(bool connected, uint32_t connectMs, uint16_t attempts);
/// Résultat NTP confirmé (journalise ntp_ok / ntp_fail ; conditionne le repli heure serveur).
void n3ContactDiagOnNtpResult(bool confirmed);

bool n3ContactDiagIsSafeMode();
uint8_t n3ContactDiagFailedBoots();
bool n3ContactDiagIsHealthy();
bool n3ContactDiagHasClockOffset();
int32_t n3ContactDiagClockOffsetSeconds();

/// Remplit jusqu'à `max` champs enrichis pour N3HeartbeatConfig::extraFields ; retourne le nombre.
/// Les String restent valides tant que `out` vit (les remplir juste avant n3DataSendHeartbeat).
size_t n3ContactDiagHeartbeatFields(N3DataField* out, size_t max);

uint8_t n3ContactDiagPendingCount();

/// Envoie les événements non acquittés à /ota-report et acquitte les séquences confirmées.
/// Sans heure fiable (epochProvider() == 0) rien n'est envoyé sauf `force` (le serveur exige
/// une signature valide : la tentative sert alors de trace de rejet).
bool n3ContactDiagFlush(bool force = false);

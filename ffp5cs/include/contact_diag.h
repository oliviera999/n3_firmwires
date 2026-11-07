#pragma once
// =============================================================================
// contact_diag.h — Diagnostic de perte de contact (v15.34)
// =============================================================================
// Incident FFP3 du 06/11/2026 (n3_serveur docs/DIAGNOSTIC_FFP3_PERTE_CONTACT_2026-11-06.md) :
// le module s'est tu après une OTA sans qu'aucune trace ne survive. Ce module rassemble :
//   • le journal de bord persistant (anneau n3evt::Ring en NVS, séquences acquittées par le
//     serveur via POST /ota-report, renvoyées jusqu'à acquittement) ;
//   • le diagnostic de boot (cause de reset, partition, état OTA de l'image, boots ratés
//     consécutifs, mode sans échec après n3boot::kSafeModeAfterFailedBoots) ;
//   • le décalage d'horloge mesuré d'après l'en-tête X-Server-Ts renvoyé par le serveur ;
//   • les champs enrichis du heartbeat (contracts/ffp3.json → heartbeatFields, n3_serveur ≥ 6.59.0).
// Un boot est « sain » quand Wi-Fi + heure fiable + un POST authentifié accepté ont été obtenus :
// c'est ce critère (et non plus un simple uptime) qui valide une image OTA en probation.
// Logique pure dans shared/ : n3_common (n3_event_ring.h, n3_boot_diag.h) et n3_time (n3_sleep_clock.h).
// =============================================================================
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

namespace ContactDiag {

// Capacité de l'anneau persistant (12 × ≤128 o en NVS « logs », clés evt_00..evt_11).
constexpr uint8_t kRingCapacity = 12;
// Budget maximal du JSON `log=` dans un heartbeat (le reste part par /ota-report).
constexpr size_t kHeartbeatLogMaxChars = 360;
// Durée pendant laquelle une heure serveur (X-Server-Ts) rend l'horloge « fiable » sans NTP.
constexpr uint32_t kServerTimeTrustMs = 6UL * 3600UL * 1000UL;

/// À appeler dès que la NVS est prête (avant Wi-Fi) : charge l'anneau, incrémente et persiste le
/// compteur de boots ratés (un crash immédiat compte), décide du mode sans échec, journalise `boot`.
void begin();

/// Journalise un événement (code court ≤ 23 car., détail ≤ 63 car.) et persiste le slot en NVS.
/// Un même code répété en moins de 30 s est ignoré (anti-rafale sur les erreurs HTTP).
void logEvent(const char* event, const char* detail = nullptr);
void logEventf(const char* event, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

/// Mode sans échec : ≥ kSafeModeAfterFailedBoots boots consécutifs sans contact sain.
bool isSafeMode();
uint8_t failedBoots();

/// Boot sain = Wi-Fi + heure fiable + POST authentifié accepté depuis ce boot.
bool isBootHealthy();
/// À appeler par WebClient sur le premier 2xx d'une requête signée (HMAC) : remet le compteur
/// de boots ratés à zéro, journalise `contact_ok`, et permet la validation de l'image OTA.
void markBootHealthy();

void setWifiConnectStats(uint32_t connectMs, uint16_t attempts);

/// Heure serveur reçue (X-Server-Ts) avec la durée aller-retour mesurée : calcule le décalage
/// module − serveur, l'expose au heartbeat, journalise une dérive > 60 s, et applique l'heure si
/// l'horloge n'est pas fiable (PowerManager::applyServerEpoch).
void onServerTime(uint32_t serverEpoch, uint32_t rttMs);
bool hasClockOffset();
int32_t clockOffsetSeconds();

const char* resetReasonLabel();
const char* partitionLabel();
const char* otaStateLabel();

/// Ajoute à `buf` (déjà rempli, NUL-terminé) les champs enrichis du heartbeat :
/// &rssi&version&rst&part&ota&fails&safe[&clk][&wifi_ms&wifi_tries]&board[&boot=1][&log=[…]].
/// `boot=1` n'est émis qu'une fois par boot. Retourne la longueur finale.
size_t appendHeartbeatExtras(char* buf, size_t size);

/// Nombre d'événements non acquittés.
uint8_t pendingCount();

/// Envoie les événements non acquittés à POST /ota-report (HMAC obligatoire côté serveur : rien
/// n'est envoyé si l'horloge n'est pas fiable, sauf `force` qui tente quand même) et acquitte
/// les séquences confirmées. Appelé après chaque heartbeat et autour des étapes OTA.
bool flushPending(bool force = false);

}  // namespace ContactDiag

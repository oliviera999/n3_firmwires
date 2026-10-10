#ifndef N3_TIME_H
#define N3_TIME_H

#include <Arduino.h>
#include <Preferences.h>
#include <stdint.h>

class ESP32Time;

#ifndef N3_TIME_MIN_VALID_EPOCH
#define N3_TIME_MIN_VALID_EPOCH 1577836800UL  /* 2020-01-01 */
#endif

void n3TimeSaveToFlash(ESP32Time& rtc, Preferences& prefs);
void n3TimeLoadFromFlash(Preferences& prefs, ESP32Time& rtc);

/**
 * Imprime la raison du réveil (libellés EN) et recharge l'heure NVS selon le cas.
 *
 * @param loadNvsOnTimerWake  Recharger l'epoch NVS (n3TimeLoadFromFlash, qui
 *   fait un settimeofday écrasant l'horloge) au réveil TIMER.
 *   - true  : besoin uploadphotosserver (horloge perdue au deep sleep CAM).
 *   - false : n3pp/msp — leur horloge RTC survit au réveil timer ; recharger un
 *     epoch NVS potentiellement périmé la ferait dériver en régression.
 *   Le cas `default` (boot hors deep sleep) recharge TOUJOURS la NVS, quel que
 *   soit ce paramètre. Sans valeur par défaut : chaque appelant DOIT choisir
 *   explicitement le comportement risqué au réveil TIMER (contrat T1.3).
 */
void n3PrintWakeupReason(Preferences& prefs, ESP32Time& rtc,
                         bool loadNvsOnTimerWake);

/** Charge l'epoch NVS dans rtc et l'injecte dans l'horloge système si plausible. */
bool n3TimeLoadAndApplyToSystem(Preferences& prefs, ESP32Time& rtc);

/**
 * configTime + attente getLocalTime ; met à jour rtc si succès.
 *
 * ⚠ getLocalTime() réussit dès que l'horloge système est PLAUSIBLE (année > 2016),
 * même périmée : si l'horloge est déjà amorcée (epoch NVS rechargé, horloge RTC
 * après deep sleep), cette fonction retourne true IMMÉDIATEMENT, sans qu'aucune
 * réponse NTP n'ait été reçue. Pour une synchro réellement confirmée, utiliser
 * n3TimeSyncNtpConfirmed().
 */
bool n3TimeSyncNtp(ESP32Time& rtc,
                   long gmtOffsetSec,
                   int daylightOffsetSec,
                   const char* ntpServer,
                   uint32_t timeoutMs);

/**
 * NTP CONFIRMÉ : configTime puis attente de la fin effective d'une synchro SNTP
 * (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED), indépendamment de
 * l'état de l'horloge avant l'appel. true = une réponse NTP a réellement été
 * appliquée pendant l'appel (rtc mis à jour) ; false = timeout, horloge inchangée.
 */
bool n3TimeSyncNtpConfirmed(ESP32Time& rtc,
                            long gmtOffsetSec,
                            int daylightOffsetSec,
                            const char* ntpServer,
                            uint32_t timeoutMs);

/**
 * Epoch brut persisté par n3TimeSaveToFlash (NVS "rtc"/"epoch"), SANS le repli
 * calendaire de n3TimeLoadFromFlash (12:00 le 01/01/2023 si la clé est absente :
 * date « plausible » mais fausse). Retourne 0 si aucun epoch n'a été persisté.
 */
unsigned long n3TimeReadSavedEpoch(Preferences& prefs);

/** Horloge système plausible (epoch > seuil 2020). */
bool n3TimeHasPlausibleEpoch(void);

/**
 * Resynchronise les 6 composantes calendaires d'un firmware depuis le RTC.
 * Mutualisé n3pp/msp : ce bloc était dupliqué 3× (print_wakeup_reason n3pp/msp
 * + HeureSansWifi n3pp). annee est sur 4 chiffres (%Y).
 */
void n3TimeSyncBrokenDown(ESP32Time& rtc, int& seconde, int& minute, int& heure,
                          int& jour, int& mois, int& annee);

#endif

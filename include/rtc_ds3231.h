#pragma once

/**
 * @file rtc_ds3231.h
 * @brief Driver minimal pour RTC DS3231 sur I2C (heure précise offline).
 *
 * Utilise I2CBusGuard ; à appeler après I2CBus::init().
 * Heure lue/écrite en heure locale (cohérent avec timezone configurée).
 */

#include <time.h>

namespace RtcDS3231 {

/** Vérifie si un DS3231 répond à l'adresse I2C (Pins::DS3231_I2C_ADDR). */
bool isPresent();

/**
 * Lit la date/heure du DS3231 (heure locale) et la convertit en epoch.
 * @param outEpoch reçoit l'epoch (UTC) correspondant à l'heure locale lue
 * @return true si lecture et conversion OK, false sinon
 */
bool read(time_t* outEpoch);

/**
 * Écrit l'epoch (UTC) dans le DS3231 en heure locale.
 * @param epoch temps à écrire (interprété via localtime_r)
 * @return true si écriture OK, false sinon
 */
bool write(time_t epoch);

}  // namespace RtcDS3231

#pragma once
/**
 * Banc energie — console serie (115200 bauds) : trame CSV 1 Hz + commandes de
 * decouverte / calibration des INA226. Taper « help » dans le moniteur serie.
 */

#include <Arduino.h>

void consoleBegin();
/** Lit les caracteres disponibles et execute une commande par ligne recue. */
void consolePoll();
/** Ligne CSV de la mesure courante (si activee). */
void consolePrintCsv(uint32_t nowMs);
/** true si l'utilisateur a demande un envoi immediat (« post »). */
bool consoleConsumePostRequest();

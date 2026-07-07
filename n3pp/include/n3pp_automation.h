#pragma once

#include "n3_notify.h"

void HeureSansWifi();
void EnregistrementHeureFlash();
// Envoi d'un mail d'alerte typé : la sévérité est filtrée par le mode de
// notification courant (dérivé de enableEmailChecked) et le sujet est préfixé
// "[N3PP][Pn]" pour le tri côté boîte mail.
// Retour (Phase 0 arbitrage mails) : true si le mail a été LIVRÉ (SMTP OK) ou
// volontairement filtré par le mode de notification (traité selon la politique) ;
// false si l'envoi SMTP a échoué. Les flags anti-spam ne doivent être latchés
// que sur retour true, pour ne plus perdre d'alertes hors ligne.
bool sendEmailNotification(N3Severity severity);
// Mode de notification courant (none/important/partial/full) issu de la config
// distante enableEmailChecked (rétro-compatible "checked"/"unchecked").
N3NotifMode n3ppNotifMode();
void arrosage();
bool arrosageAutoCooldownExpired();
void arrosageAutoAccumulateCooldown(int sleepSeconds);
void automatismes();
void sommeil();
void print_wakeup_reason();

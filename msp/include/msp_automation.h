/* MeteoStationPrototype (msp1) — Automatismes
 * sendEmailNotification, sommeil, EnregistrementHeureFlash, print_wakeup_reason
 */
#pragma once

#include "n3_notify.h"

// Envoi d'un mail d'alerte typé : la sévérité est filtrée par le mode de
// notification courant (dérivé de enableEmailChecked) et le sujet est préfixé
// "[MSP1][Pn]".
void sendEmailNotification(N3Severity severity);
// Mode de notification courant (none/important/partial/full) issu de la config
// distante enableEmailChecked (rétro-compatible "checked"/"unchecked").
N3NotifMode mspNotifMode();
void sommeil();
void EnregistrementHeureFlash();
void print_wakeup_reason();

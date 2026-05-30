#pragma once

void HeureSansWifi();
void EnregistrementHeureFlash();
void sendEmailNotification();
void arrosage();
bool arrosageAutoCooldownExpired();
void arrosageAutoAccumulateCooldown(int sleepSeconds);
void automatismes();
void sommeil();
void print_wakeup_reason();

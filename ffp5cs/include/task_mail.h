#pragma once
//
// task_mail.h — Gestion mail/SMTP côté tâches : réserve heap 32KB + traitement
// séquentiel de la file de mails. Extrait d'app_tasks.cpp (refonte god-file,
// audit v13.93). Le concern mail (réserve + processMailQueueIfReady) est isolé ;
// app_tasks et les corps de tâches déplacés y accèdent via cette API.
//
#include "config.h"   // FEATURE_MAIL
#include <stdint.h>

struct AppContext;

#if FEATURE_MAIL
// Alloue la réserve 32KB (PSRAM en priorité sur S3) si pas déjà présente.
void allocMailReserveIfNeeded(uint32_t kMailBlockReq);
// Traitement séquentiel d'un mail en attente selon le heap disponible.
void processMailQueueIfReady(AppContext* ctx, unsigned long now);
// Libère la réserve uniquement si elle est en RAM interne (pour créer un bloc
// contigu, ex. avant OTA). Retourne true si une libération a eu lieu, false
// sinon (absente ou en PSRAM).
bool mailReserveReleaseIfInternal();
#endif // FEATURE_MAIL

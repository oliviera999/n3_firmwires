// mailer_queue.cpp — file d'attente mail + envois asynchrones de Mailer :
// getQueuedMails / initMailQueue / processOneMailSync / hasPendingMails / send / sendAlert.
// Extrait de mailer.cpp (audit : découpe god-file). Méthodes membres ; les envois
// asynchrones délèguent à sendSync()/sendAlertSync() (définis dans mailer.cpp).
// Garde FEATURE_MAIL : quand le mail est désactivé, les stubs (#else) de mailer.cpp
// fournissent déjà ces méthodes ; on ne compile pas les vraies implémentations ici.
#include "mailer.h"  // Mailer, MailQueueItem, config.h (TaskConfig/EmailConfig), FreeRTOS queue
#include <cstring>   // memset/strncpy (send())

#if FEATURE_MAIL && FEATURE_MAIL != 0

// ============================================================================
// MÉTHODES ASYNCHRONES (v11.142) - Non-bloquantes
// Ces méthodes ajoutent le mail à une queue et retournent immédiatement.
// v11.155: Traitement séquentiel depuis automationTask (plus de tâche dédiée)
// ============================================================================

uint32_t Mailer::getQueuedMails() const {
  if (!_mailQueue) return 0;
  return uxQueueMessagesWaiting(_mailQueue);
}

// Initialisation de la queue mail (sans tâche dédiée - v11.155: séquentiel)
bool Mailer::initMailQueue() {
  Serial.println(F("[Mail] >>> INITIALISATION QUEUE MAIL SEQUENTIELLE <<<"));
  
  // Créer la queue de mails
  _mailQueue = xQueueCreate(TaskConfig::MAIL_QUEUE_SIZE, sizeof(MailQueueItem));
  if (!_mailQueue) {
    Serial.println(F("[Mail] ❌ Échec création queue mail"));
    return false;
  }
  Serial.printf("[Mail] ✅ Queue mail créée (%d slots, traitement séquentiel)\n", TaskConfig::MAIL_QUEUE_SIZE);
  
  return true;
}

// Traitement séquentiel d'un mail depuis la queue (appelé depuis automationTask)
// Retourne true si un mail a été traité, false si aucun mail en attente
bool Mailer::processOneMailSync() {
  if (!_mailQueue) {
    return false; // Queue non initialisée
  }
  
  MailQueueItem item;
  
  // Lire un mail de la queue (non-bloquant)
  if (xQueueReceive(_mailQueue, &item, 0) != pdTRUE) {
    return false; // Aucun mail en attente
  }
  
  Serial.printf("[Mail] 📬 Traitement mail séquentiel: '%s'\n", item.subject);
  Serial.println(F("[Mail] >>> ENVOI SMTP DÉBUT <<<"));  // Témoin début envoi effectif

  bool success;
  if (item.isAlert) {
    success = sendAlertSync(item.subject, item.message, item.toEmail, item.includeDetailedReport);
  } else {
    success = sendSync(item.subject, item.message, "User", item.toEmail);
  }
  
  if (success) {
    _mailsSent++;
    Serial.printf("[Mail] ✅ Mail SMTP envoyé avec succès (%u total)\n", _mailsSent);
    Serial.println(F("[Mail] ENVOI SMTP EFFECTIF: OK"));  // Témoin pour analyse log / scripts
  } else {
    // Re-queue une fois pour retry (échec transitoire WiFi/TLS), max 2 tentatives au total
    if (item.retryCount < 2) {
      item.retryCount++;
      if (xQueueSendToFront(_mailQueue, &item, 0) == pdTRUE) {
        Serial.printf("[Mail] 🔄 Mail remis en queue (retry %u/2): '%s'\n", item.retryCount, item.subject);
      } else {
        _mailsFailed++;
        Serial.printf("[Mail] ❌ Échec envoi mail (%u échecs)\n", _mailsFailed);
        Serial.println(F("[Mail] ENVOI SMTP EFFECTIF: KO"));
      }
    } else {
      _mailsFailed++;
      Serial.printf("[Mail] ❌ Échec envoi mail (%u échecs)\n", _mailsFailed);
      Serial.println(F("[Mail] ENVOI SMTP EFFECTIF: KO"));  // Témoin pour analyse log / scripts
    }
  }
  
  return true; // Un mail a été traité
}

// Vérification si des mails sont en attente
bool Mailer::hasPendingMails() const {
  if (!_mailQueue) return false;
  return uxQueueMessagesWaiting(_mailQueue) > 0;
}

// Méthode send() asynchrone - ajoute à la queue et retourne immédiatement
bool Mailer::send(const char* subject, const char* message, const char* toName, const char* toEmail) {
  (void)toName; // Non utilisé dans la version asynchrone
  
  if (!_mailQueue) {
    Serial.println(F("[Mail] ⚠️ Queue non initialisée, envoi synchrone..."));
    return sendSync(subject, message, toName, toEmail);
  }
  
  MailQueueItem item;
  memset(&item, 0, sizeof(item));
  
  // Copie sécurisée des données (null-termination explicite après strncpy)
  if (subject) {
    strncpy(item.subject, subject, sizeof(item.subject) - 1);
    item.subject[sizeof(item.subject) - 1] = '\0';
  }
  if (message) {
    strncpy(item.message, message, sizeof(item.message) - 1);
    item.message[sizeof(item.message) - 1] = '\0';
  }
  if (toEmail) {
    strncpy(item.toEmail, toEmail, sizeof(item.toEmail) - 1);
    item.toEmail[sizeof(item.toEmail) - 1] = '\0';
  } else {
    strncpy(item.toEmail, EmailConfig::DEFAULT_RECIPIENT, sizeof(item.toEmail) - 1);
    item.toEmail[sizeof(item.toEmail) - 1] = '\0';
  }
  item.isAlert = false;
  item.retryCount = 0;
  
  // Ajoute à la queue (timeout 100ms), retry une fois après 200ms si pleine (robustesse)
  if (xQueueSend(_mailQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
    vTaskDelay(pdMS_TO_TICKS(200));
    if (xQueueSend(_mailQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.println(F("[Mail] ⚠️ Queue pleine, mail ignoré"));
      return false;
    }
  }
  
  Serial.printf("[Mail] 📥 Mail ajouté à la queue (%u en attente): '%s'\n",
                getQueuedMails(), subject);
  return true;
}

// Méthode sendAlert() asynchrone - ajoute à la queue et retourne immédiatement
bool Mailer::sendAlert(const char* subject, const char* message, const char* toEmail, bool includeDetailedReport) {
  Serial.println(F("[Mail] ===== SENDALERT ASYNC (v11.142) ====="));
  
  if (!_mailQueue) {
    Serial.println(F("[Mail] ⚠️ Queue non initialisée, envoi synchrone..."));
    return sendAlertSync(subject, message, toEmail, includeDetailedReport);
  }
  
  // Vérifications préalables
  if (!subject) {
    Serial.println(F("[Mail] ❌ ERREUR: subject est NULL"));
    return false;
  }
  if (!message || strlen(message) == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: message vide"));
    return false;
  }
  
  MailQueueItem item;
  memset(&item, 0, sizeof(item));
  
  // Copie sécurisée des données (null-termination explicite après strncpy)
  strncpy(item.subject, subject, sizeof(item.subject) - 1);
  item.subject[sizeof(item.subject) - 1] = '\0';

  // Tronquer le message si trop long
  size_t msgLen = strlen(message);
  if (msgLen >= sizeof(item.message)) {
    Serial.printf("[Mail] ⚠️ Message tronqué de %u à %u caractères\n", 
                  msgLen, sizeof(item.message) - 1);
    msgLen = sizeof(item.message) - 1;
  }
  strncpy(item.message, message, msgLen);
  item.message[msgLen] = '\0';
  
  // Utiliser fallback si toEmail vide (cohérent avec send())
  if (toEmail && strlen(toEmail) > 0) {
    strncpy(item.toEmail, toEmail, sizeof(item.toEmail) - 1);
    item.toEmail[sizeof(item.toEmail) - 1] = '\0';
  } else {
    Serial.println(F("[Mail] ⚠️ toEmail vide, utilisation DEFAULT_RECIPIENT"));
    strncpy(item.toEmail, EmailConfig::DEFAULT_RECIPIENT, sizeof(item.toEmail) - 1);
    item.toEmail[sizeof(item.toEmail) - 1] = '\0';
  }
  item.isAlert = true;
  item.includeDetailedReport = includeDetailedReport;
  item.retryCount = 0;
  
  // Ajoute à la queue (timeout 100ms), retry une fois après 200ms si pleine (robustesse)
  if (xQueueSend(_mailQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
    vTaskDelay(pdMS_TO_TICKS(200));
    if (xQueueSend(_mailQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.println(F("[Mail] ⚠️ Queue pleine, alerte ignorée"));
      return false;
    }
  }
  
  Serial.printf("[Mail] 📥 Alerte ajoutée à la queue (%u en attente): '%s'\n", 
                getQueuedMails(), subject);
  Serial.println(F("[Mail] ✅ Retour immédiat (non-bloquant)"));
  return true;
}

#endif  // FEATURE_MAIL

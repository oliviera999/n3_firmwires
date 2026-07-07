// Mailer implementation can be compiled out to save flash on ESP32 WROOM
#include "mailer.h"
#include "config.h"
#include "power.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "diagnostics.h"
#include "nvs_manager.h"
#include "nvs_keys.h"
#include "tls_mutex.h"  // v11.149: Mutex pour sérialiser TLS (SMTP/HTTPS)
#include <WiFi.h>
#include "wifi_manager.h"  // Pour WiFiHelpers
#include <time.h>
#include "ffp5cs_fs.h"
#include "uptime_format.h"  // Logique pure de formatage d'uptime (extraite, testée nativement)
#include <esp_task_wdt.h> // Pour esp_task_wdt_reset() dans mailTask
#include <esp_heap_caps.h>
#include <cstring>

#if FEATURE_MAIL && FEATURE_MAIL != 0

// Mutualisation Phase 1 : construction du SMTP_Message + MailClient.sendMail
// factorisee dans la lib partagee shared/n3_mail (n3MailSendMessageWithSession).
// On conserve ici la session persistante _smtp, le mutex TLS, les feeds watchdog,
// la garde heap et la fermeture de session ; seule la plomberie ESP_Mail_Client du
// message est deleguee. mailer.h a deja inclus <ESP_Mail_Client.h> (FEATURE_MAIL),
// donc le type SMTPSession est complet ici (n3_mail.h ne fait qu'une forward-decl).
#include "n3_mail.h"

// Buffer statique pour formatUptime (conforme .cursorrules)
static char g_uptimeBuffer[48];

// Construit un bloc d'informations système (réseau, version, mémoire, uptime)
// Délègue la logique pure à UptimeFormat::formatUptime (uptime_format.h, testé
// nativement) ; conserve le buffer statique g_uptimeBuffer (parité .cursorrules).
static const char* formatUptime(unsigned long ms) {
  return UptimeFormat::formatUptime(ms, g_uptimeBuffer, sizeof(g_uptimeBuffer));
}

// Buffers statiques pour éviter fragmentation mémoire (conforme .cursorrules)
// v11.144: Réduits pour économiser ~4.5 KB de RAM
static char g_detailedTimeReportBuffer[512];  // Réduit de 2048
static char g_lightFooterBuffer[256];         // Footer allégé
// Piste 4 rapport mémoire: un seul buffer pour sendSync et sendAlertSync (évite ~2.5 KB)
static char s_mailMessageBuffer[BufferConfig::EMAIL_MAX_SIZE_BYTES + 512];
// Mise en veille et réveil ne surviennent jamais simultanément (on s'endort puis
// on se réveille) : un seul buffer partagé suffit pour le sujet et le corps des
// deux mails. Économie ~1 KB de DRAM interne, ressource critique sur ESP32-WROOM
// (segment dram0_0_seg quasi saturé en profil prod/beta).
static char g_sleepWakeSubject[64];
static char g_sleepWakeMessage[1024];
// v11.178: kLittleFsLabel supprimé (non utilisé - audit dead-code)

// ======================
// FONCTIONS HELPER POUR ÉVITER LA DUPLICATION
// ======================

// Affiche une chaîne en ASCII sûr (0x20–0x7E) pour les logs série (évite caractères UTF-8 illisibles)
static void logSafeStr(const char* s, size_t maxLen) {
  if (!s) return;
  for (size_t i = 0; i < maxLen && s[i]; i++) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    Serial.print((c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '?');
  }
}

// Epoch validé pour affichage : délègue à PowerManager si disponible (évite doublon NVS/fallback).
static PowerManager* s_powerForEpoch = nullptr;
static time_t getSafeEpochForDisplay() {
  if (s_powerForEpoch) {
    return s_powerForEpoch->getCurrentEpochSafe();
  }
  time_t t = time(nullptr);
  if (t >= SleepConfig::EPOCH_MIN_VALID && t <= SleepConfig::EPOCH_MAX_VALID && t != 0) {
    return t;
  }
  unsigned long saved;
  g_nvsManager.loadULong(NVS_NAMESPACES::SYSTEM, NVSKeys::System::RTC_EPOCH, saved, 0);
  time_t savedEpoch = static_cast<time_t>(saved);
  if (savedEpoch >= SleepConfig::EPOCH_MIN_VALID && savedEpoch <= SleepConfig::EPOCH_MAX_VALID) {
    return savedEpoch;
  }
  return SleepConfig::EPOCH_DEFAULT_FALLBACK;
}

// ======================
// FOOTER ALLÉGÉ
// ======================

// Accès état sync pour footer (défini dans app.cpp, évite dépendance circulaire)
extern unsigned long ffp5csGetLastSendMsForMail();
extern int ffp5csGetLastDataSkipReasonForMail();

// Footer allégé : version + heure + temp eau + infos mémoire + indicateur data (diagnostic à distance)
static const char* buildLightFooter() {
  char* buf = g_lightFooterBuffer;
  size_t remaining = sizeof(g_lightFooterBuffer);
  time_t now = getSafeEpochForDisplay();
  char timeBuf[24] = "(heure N/A)";
  if (now >= SleepConfig::EPOCH_MIN_VALID && now <= SleepConfig::EPOCH_MAX_VALID) {
    struct tm tmInfo;
    if (localtime_r(&now, &tmInfo)) {
      strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", &tmInfo);
    }
  }
  // v13.65 (audit): utiliser le cache au lieu de sensors.read() (1-7s bloquant) pour
  // remplir le footer mail. Si pas de cache valide, utiliser la valeur fallback.
  extern SystemSensors sensors;
  float tempWater;
  {
    SensorReadings cached{};
    if (sensors.getLastCachedReadings(cached) && !isnan(cached.tempWater)) {
      tempWater = cached.tempWater;
    } else {
      tempWater = SensorConfig::Fallback::TEMP_WATER;
    }
  }
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t minHeap = ESP.getMinFreeHeap();
  size_t psramSize = ESP.getPsramSize();
  int n;
  if (psramSize > 0) {
    n = snprintf(buf, remaining,
                 "\n[FFP5CS v%s] %s | Eau %.1f°C\n"
                 "- Mémoire: heap libre %u, min %u bytes | PSRAM libre %u bytes",
                 ProjectConfig::VERSION, timeBuf, tempWater,
                 (unsigned)freeHeap, (unsigned)minHeap, (unsigned)ESP.getFreePsram());
  } else {
    n = snprintf(buf, remaining,
                 "\n[FFP5CS v%s] %s | Eau %.1f°C\n"
                 "- Mémoire: heap libre %u, min %u bytes",
                 ProjectConfig::VERSION, timeBuf, tempWater,
                 (unsigned)freeHeap, (unsigned)minHeap);
  }
  if (n < 0 || (size_t)n >= remaining) {
    buf[remaining - 1] = '\0';
  } else {
    size_t used = strlen(buf);
    remaining = sizeof(g_lightFooterBuffer) - used - 1;
    if (remaining > 32) {
      unsigned long lastSendMs = ffp5csGetLastSendMsForMail();
      int skipReason = ffp5csGetLastDataSkipReasonForMail();
      const char* dataLine;
      if (lastSendMs == 0) {
        dataLine = "\n- Data: jamais envoyé";
      } else if (skipReason == 1) {
        dataLine = "\n- Data: non envoyé (mémoire basse)";
      } else if (skipReason == 2) {
        dataLine = "\n- Data: dernier échec (réseau/timeout)";
      } else {
        unsigned long agoMin = (millis() - lastSendMs) / 60000UL;
        (void)snprintf(buf + used, remaining, "\n- Data: OK (il y a %lu min)", agoMin);
        dataLine = nullptr;  // déjà écrit
      }
      if (dataLine) {
        strncat(buf, dataLine, remaining - 1);
        buf[sizeof(g_lightFooterBuffer) - 1] = '\0';
      }
    }
  }
  return g_lightFooterBuffer;
}

// Fonction pour générer un rapport temporel détaillé
static const char* buildDetailedTimeReport(const Diagnostics& diagnostics) {
  char* buf = g_detailedTimeReportBuffer;
  size_t remaining = sizeof(g_detailedTimeReportBuffer);
  int written = 0;
  
  written = snprintf(buf, remaining, "\n\n-- RAPPORT TEMPOREL DÉTAILLÉ --\n");
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return buf;
  }
  buf += written;
  remaining -= written;
  
  time_t now = getSafeEpochForDisplay();
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    // Utiliser le helper pour les infos temporelles de base
    // Mais on doit d'abord ajouter le header et les infos spécifiques
    written = snprintf(buf, remaining, "Heure actuelle: %s\n"
                                       "Epoch: %lu\n"
                                       "Jour de la semaine: %d (0=dimanche)\n"
                                       "Jour de l'année: %d\n"
                                       "DST actif: %s\n",
                       timeBuf, (unsigned long)now, timeinfo.tm_wday, timeinfo.tm_yday,
                       timeinfo.tm_isdst ? "OUI" : "NON");
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_detailedTimeReportBuffer;
    }
    buf += written;
    remaining -= written;
    
    // Uptime (utiliser formatUptime)
    const char* uptimeStr = formatUptime(millis());
    written = snprintf(buf, remaining, "Uptime: %s\n", uptimeStr);
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_detailedTimeReportBuffer;
    }
    buf += written;
    remaining -= written;
    
    // Informations NTP
    written = snprintf(buf, remaining, "Serveur NTP: %s\n"
                                       "GMT Offset: +%ldh\n"
                                       "DST Offset: +%ldh\n",
                       SystemConfig::NTP_SERVER,
                       SystemConfig::NTP_GMT_OFFSET_SEC/3600,
                       SystemConfig::NTP_DAYLIGHT_OFFSET_SEC/3600);
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_detailedTimeReportBuffer;
    }
    buf += written;
    remaining -= written;
    
    // Informations RTC/Flash (SYSTEM namespace)
    unsigned long savedEpoch;
    g_nvsManager.loadULong(NVS_NAMESPACES::SYSTEM, NVSKeys::System::RTC_EPOCH, savedEpoch, 0);
    if (savedEpoch > 0) {
      written = snprintf(buf, remaining, "RTC Flash epoch: %lu\n", savedEpoch);
      if (written < 0 || (size_t)written >= remaining) {
        buf[remaining - 1] = '\0';
        return g_detailedTimeReportBuffer;
      }
      buf += written;
      remaining -= written;
      
      if (savedEpoch != (unsigned long)now) {
        long diff = (long)now - (long)savedEpoch;
        if (abs(diff) > 60) {
          written = snprintf(buf, remaining, "Diff RTC vs actuel: %ld secondes\n"
                                             "⚠️ Écart important entre RTC et temps actuel!\n",
                             diff);
        } else {
          written = snprintf(buf, remaining, "Diff RTC vs actuel: %ld secondes\n", diff);
        }
        if (written < 0 || (size_t)written >= remaining) {
          buf[remaining - 1] = '\0';
          return g_detailedTimeReportBuffer;
        }
        buf += written;
        remaining -= written;
      }
    }
  } else {
    written = snprintf(buf, remaining, "Erreur: Impossible de récupérer l'heure locale\n");
    if (written < 0 || (size_t)written >= remaining) {
      buf[remaining - 1] = '\0';
      return g_detailedTimeReportBuffer;
    }
    buf += written;
    remaining -= written;
  }
  
  // Ajouter les informations de redémarrage
  written = snprintf(buf, remaining, "\n-- INFORMATIONS DE REDÉMARRAGE --\n");
  if (written < 0 || (size_t)written >= remaining) {
    buf[remaining - 1] = '\0';
    return g_detailedTimeReportBuffer;
  }
  buf += written;
  remaining -= written;
  
  // Ajouter le rapport de redémarrage (utilise buffer statique)
  char restartReportBuffer[2048];
  diagnostics.generateRestartReport(restartReportBuffer, sizeof(restartReportBuffer));
  size_t restartLen = strlen(restartReportBuffer);
  if (restartLen > 0 && restartLen < remaining) {
    strncpy(buf, restartReportBuffer, remaining - 1);
    buf[remaining - 1] = '\0';
  }
  
  return g_detailedTimeReportBuffer;
}

void Mailer::setPowerManager(PowerManager* power) {
  _power = power;
  s_powerForEpoch = power;
}

bool Mailer::begin() {
  Serial.println(F("[Mail] ===== INITIALISATION MAILER ====="));
  
  // Prépare uniquement la configuration SMTP.
  // La connexion TLS/SMTP sera établie à la première utilisation dans send().

  _cfg = Session_Config();
  _cfg.server.host_name = EmailConfig::SMTP_HOST;
  _cfg.server.port      = EmailConfig::SMTP_PORT;
  _cfg.login.email      = Secrets::AUTHOR_EMAIL;
  _cfg.login.password   = Secrets::AUTHOR_PASSWORD;

  // v11.151: Reconnexion automatique et timeout conforme à .cursorrules (max 5s)
  MailClient.networkReconnect(true);
  _smtp.setTCPTimeout(5);  // 5 secondes de timeout TCP (conforme .cursorrules: max 5s pour opérations réseau)

  // Vérifications de configuration
  if (!EmailConfig::SMTP_HOST || strlen(EmailConfig::SMTP_HOST) == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: SMTP_HOST non configuré"));
    return false;
  }
  if (!Secrets::AUTHOR_EMAIL || strlen(Secrets::AUTHOR_EMAIL) == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: AUTHOR_EMAIL non configuré"));
    return false;
  }
  if (!Secrets::AUTHOR_PASSWORD || strlen(Secrets::AUTHOR_PASSWORD) == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: AUTHOR_PASSWORD non configuré"));
    return false;
  }
  
  // Pas de connexion ici pour éviter le blocage au démarrage
  _ready = false;
  Serial.println(F("[Mail] ✅ Configuration SMTP prête (connexion différée)"));
  Serial.println(F("[Mail] ===== FIN INITIALISATION MAILER ====="));
  return true;
}

// Fonction d'attente réseau pour SMTP (similaire à PowerManager::waitForNetworkReady)
static bool waitForNetworkReadyForSMTP() {
  wl_status_t st = WiFi.status();
  if (st != WL_CONNECTED) {
    Serial.printf("[Mail] waitForNetworkReady: WiFi non connecté (status=%d), abandon\n", (int)st);
    return false;
  }
  
  // v11.165: Timeouts réduits (règle offline-first: max 3s blocage)
  const uint32_t STABILIZATION_DELAY_MS = 1000;  // 1 seconde de stabilisation
  const uint32_t MAX_WAIT_MS = 3000;             // 3 secondes max d'attente totale
  uint32_t startMs = millis();
  
  Serial.println(F("[Mail] Attente stabilisation réseau pour SMTP..."));
  
  // Phase 1: Délai minimum de stabilisation TCP/IP
  vTaskDelay(pdMS_TO_TICKS(STABILIZATION_DELAY_MS));
  
  // Phase 2: Vérifier que l'IP est toujours valide et DNS fonctionne
  while ((millis() - startMs) < MAX_WAIT_MS) {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      esp_task_wdt_reset();  // Plan: watchdog dans boucles longues
    }
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      // Test DNS rapide pour vérifier que le réseau est vraiment opérationnel
      IPAddress dnsResult;
      if (WiFi.hostByName("smtp.gmail.com", dnsResult)) {
        IPAddress localIP = WiFi.localIP();
        Serial.printf("[Mail] ✅ Réseau prêt pour SMTP (%d.%d.%d.%d, DNS OK, %lu ms)\n", 
                      localIP[0], localIP[1], localIP[2], localIP[3], millis() - startMs);
        return true;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(300));
  }
  
  // Timeout atteint mais WiFi connecté - on tente quand même
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[Mail] ⚠️ Réseau partiellement prêt après %lu ms (DNS timeout), on tente quand même\n", 
                  millis() - startMs);
    return true;
  }
  
  Serial.println(F("[Mail] ❌ Réseau perdu pendant stabilisation"));
  return false;
}

bool Mailer::sendSync(const char* subject, const char* message, const char* toName, const char* toEmail, N3Severity severity) {
  // Filtrage par degré d'importance : tout mail dont la sévérité dépasse le
  // plafond du mode courant est ignoré (retourne true = "traité", pas de retry).
  // En mode Full (défaut/legacy) rien n'est filtré -> comportement inchangé.
  if (!n3NotifModeAllows(_notifMode, severity)) {
    Serial.printf("[Mail][SKIP] severite %s filtree par le mode de notification\n",
                  n3SeverityCode(severity));
    return true;
  }

  Serial.println(F("[Mail] Trace 1: Start sendSync"));
  Serial.println(F("[Mail] ===== DIAGNOSTIC SEND ====="));
  Serial.printf("[Mail] _ready: %s\n", _ready ? "TRUE" : "FALSE");
  Serial.printf("[Mail] _smtp.connected(): %s\n", _smtp.connected() ? "TRUE" : "FALSE");
  
  // === PROTECTION SIMPLIFIÉE v11.151 ===
  // Garde seulement le mutex TLS, supprime la protection heap trop restrictive
  // Le heap bas causait le blocage de TOUS les mails
  
  // 1. Vérifier que le système n'entre pas en light sleep
  extern volatile bool g_enteringLightSleep;
  if (g_enteringLightSleep) {
    Serial.println(F("[Mail] ⛔ Envoi annulé: système en transition vers light sleep"));
    return false;
  }
  
  // 2. Log du heap (informatif seulement, n'empêche plus l'envoi)
  uint32_t freeHeap = ESP.getFreeHeap();
  Serial.printf("[Mail] 📊 Heap disponible: %u bytes\n", freeHeap);
  if (freeHeap < 40000) {
    Serial.println(F("[Mail] ⚠️ Heap bas - tentative d'envoi quand même"));
  }
  
  // 3. Acquérir le mutex TLS (empêche collision SMTP/HTTPS)
  if (!TLSMutex::acquire(10000)) {  // Timeout 10s pour SMTP
    Serial.println(F("[Mail] ⛔ Envoi annulé: impossible d'acquérir le mutex TLS"));
    return false;
  }
  
  Serial.printf("[Mail] ✅ Mutex TLS acquis (heap: %u bytes)\n", ESP.getFreeHeap());
  // === FIN PROTECTION ===
  
  // Vérifier que le réseau est prêt avant de tenter SMTP
  if (!waitForNetworkReadyForSMTP()) {
    Serial.println(F("[Mail] ❌ Réseau non prêt, abandon envoi mail"));
    TLSMutex::release();
    return false;
  }
  
  if (!_ready || !_smtp.connected()) {
    uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    if (largestBlock < HeapConfig::MIN_HEAP_BLOCK_FOR_MAIL_TLS_CONNECT) {
      Serial.printf("[Mail] ⛔ Connexion SMTP reportée: bloc contigu insuffisant (%u < %u bytes)\n",
                    (unsigned)largestBlock, (unsigned)HeapConfig::MIN_HEAP_BLOCK_FOR_MAIL_TLS_CONNECT);
      TLSMutex::release();
      return false;
    }
    Serial.println(F("[Mail] ⚠️ Connexion SMTP requise"));
    // tentative de reconnexion
    _smtp.closeSession();
    Serial.println(F("[Mail] Tentative de connexion SMTP..."));
    // v13.53 (audit): feed TWDT avant connexion SMTP TLS (peut bloquer 5-15s, TWDT 30s WROOM).
    if (esp_task_wdt_status(NULL) == ESP_OK) { esp_task_wdt_reset(); }
    _ready = _smtp.connect(&_cfg);
    if (esp_task_wdt_status(NULL) == ESP_OK) { esp_task_wdt_reset(); }
    if(!_ready){
      // v11.179: Pas de String temporaire pour éviter crash dans destructeur
      Serial.printf("[Mail] ❌ Reconnexion SMTP échouée - code: %d\n", _smtp.statusCode());
      TLSMutex::release();  // v11.151: CRITIQUE - libérer le mutex avant return !
      return false;
    } else {
      Serial.println(F("[Mail] ✅ Connexion SMTP réussie"));
    }
  } else {
    Serial.println(F("[Mail] ✅ Connexion SMTP déjà active"));
  }
  
  Serial.println(F("[Mail] Trace 2: Connection OK"));

  // FIX: Utiliser des buffers statiques pour éviter les dangling pointers
  // La bibliothèque ESP Mail Client peut utiliser les pointeurs de manière asynchrone
  static char fromNameBuf[64];
  static char subjectBuf[128];
  // Piste 4: un seul buffer partagé (s_mailMessageBuffer) pour sendSync et sendAlertSync
  
  Serial.println(F("[Mail] Trace 3: Buffers allocated"));

  // Construire l'objet avec l'environnement de manière explicite
  const char* envName = Utils::getProfileName();
  
  // Construction du nom d'expéditeur dans un buffer statique
  snprintf(fromNameBuf, sizeof(fromNameBuf), "FFP5CS [%s]", envName ? envName : "");
  
  // Construction du sujet : "[FFP5][Pn] objet" (préfixe sévérité harmonisé flotte,
  // cf. n3pp/msp). L'environnement reste visible dans le nom d'expéditeur ci-dessus.
  n3MailFormatSubject(subjectBuf, sizeof(subjectBuf), "FFP5", severity, subject ? subject : "");
  
  // Copier le message dans le buffer partagé seulement si différent (sendAlertSync passe déjà s_mailMessageBuffer)
  size_t msgLen = message ? strlen(message) : 0;
  if (message != s_mailMessageBuffer) {
    if (msgLen >= sizeof(s_mailMessageBuffer)) {
      msgLen = sizeof(s_mailMessageBuffer) - 1;
    }
    if (msgLen > 0) {
      strncpy(s_mailMessageBuffer, message, msgLen);
      s_mailMessageBuffer[msgLen] = '\0';
    } else {
      s_mailMessageBuffer[0] = '\0';
    }
  } else {
    msgLen = strlen(s_mailMessageBuffer);
  }
  
  // Vérifier si un footer complet est déjà présent (alerte critique)
  const char* footerMarker = "[Footer complet déjà inclus]";
  size_t markerLen = strlen(footerMarker);
  size_t currentLen = strlen(s_mailMessageBuffer);
  bool hasFullFooter = (currentLen >= markerLen) && 
                       (strcmp(s_mailMessageBuffer + currentLen - markerLen, footerMarker) == 0);
  
  if (hasFullFooter) {
    // Retirer le marqueur
    s_mailMessageBuffer[currentLen - markerLen] = '\0';
    Serial.println(F("[Mail] Trace 3.1: Footer complet déjà présent, pas d'ajout footer allégé"));
  } else {
    Serial.println(F("[Mail] Trace 3.1: Appending light footer..."));
    // Ajouter le footer allégé
    const char* lightFooter = buildLightFooter();
    // v11.179: Validation du pointeur (fix crash LoadProhibited)
    if (!lightFooter) {
      Serial.println(F("[Mail] ❌ buildLightFooter returned NULL"));
      lightFooter = "\n-- Footer unavailable --\n";
    }
    size_t footerLen = strlen(lightFooter);
    size_t remaining = sizeof(s_mailMessageBuffer) - currentLen - 1;
    // v11.178: Vérifier remaining > 0 avant strncat pour éviter underflow (audit bugs-high)
    if (remaining > 0 && footerLen < remaining) {
      strncat(s_mailMessageBuffer, lightFooter, remaining);
    } else if (remaining > 0) {
      // Tronquer le footer si nécessaire
      strncat(s_mailMessageBuffer, lightFooter, remaining - 1);
      s_mailMessageBuffer[sizeof(s_mailMessageBuffer) - 1] = '\0';
    }
  }
  
  Serial.println(F("[Mail] Trace 4: Message built"));

  // Configuration du message SMTP — mutualisee via shared/n3_mail.
  // Parite stricte avec l'ancien code : on ne renseigne QUE sender/subject/
  // destinataire/contenu (drapeaux charSet/encoding/priorite laisses a false,
  // comme avant). La session persistante _smtp, deja connectee ci-dessus, reste
  // possedee par ce Mailer ; n3MailSendMessageWithSession ne touche pas la connexion.
  N3MailMessageSpec mailSpec{};
  mailSpec.senderName = fromNameBuf;
  mailSpec.senderEmail = Secrets::AUTHOR_EMAIL;
  mailSpec.recipientName = toName;
  mailSpec.recipientEmail = toEmail;
  mailSpec.subject = subjectBuf;
  mailSpec.body = s_mailMessageBuffer;

  Serial.println(F("[Mail] Trace 5: Msg struct configured"));

  // Affichage des détails du mail avant envoi avec informations temporelles
  time_t mailTime = getSafeEpochForDisplay();
  struct tm mailTimeInfo;
  localtime_r(&mailTime, &mailTimeInfo);
  char mailTimeBuf[32];
  strftime(mailTimeBuf, sizeof(mailTimeBuf), "%Y-%m-%d %H:%M:%S", &mailTimeInfo);

  Serial.println(F("[Mail] ===== DÉTAILS DU MAIL ====="));
  Serial.printf("[Mail] Heure d'envoi: %s (epoch: %lu)\n", mailTimeBuf, mailTime);
  Serial.print(F("[Mail] De: "));
  logSafeStr(fromNameBuf, 60);
  Serial.print(F(" <"));
  logSafeStr(Secrets::AUTHOR_EMAIL, 60);
  Serial.println(F(">"));
  Serial.print(F("[Mail] À: "));
  logSafeStr(toName, 40);
  Serial.print(F(" <"));
  logSafeStr(toEmail, 60);
  Serial.println(F(">"));
  Serial.print(F("[Mail] Objet: "));
  logSafeStr(subjectBuf, 80);
  Serial.println();
  Serial.println(F("[Mail] Contenu (aperçu):"));
  // Afficher un aperçu du message (max 200 caractères)
  size_t previewLen = strlen(s_mailMessageBuffer);
  if (previewLen > 200) previewLen = 200;
  char preview[201];
  strncpy(preview, s_mailMessageBuffer, previewLen);
  preview[previewLen] = '\0';
  Serial.println(preview);
  Serial.println(F("[Mail] ==========================="));

  Serial.println(F("[Mail] Trace 6: Calling sendMail..."));
  // v13.53 (audit): feed TWDT avant et après MailClient.sendMail (peut bloquer 5-30s sur SMTP lent).
  if (esp_task_wdt_status(NULL) == ESP_OK) { esp_task_wdt_reset(); }
  bool ok = n3MailSendMessageWithSession(_smtp, mailSpec, nullptr);
  if (esp_task_wdt_status(NULL) == ESP_OK) { esp_task_wdt_reset(); }
  Serial.printf("[Mail] Trace 7: sendMail returned %s\n", ok ? "TRUE" : "FALSE");

  if (!ok) {
    // v11.179: Pas de String temporaire pour éviter crash dans destructeur
    Serial.printf("[Mail] ERREUR sendMail code=%d err=%d\n", _smtp.statusCode(), _smtp.errorCode());
  } else {
    Serial.println(F("[Mail] Message SMTP envoyé avec succès ✔"));
  }
  
  // CRITIQUE: Fermer la session SMTP après chaque envoi pour éviter les callbacks
  // pendants qui peuvent causer un INT_WDT avec PC=0x0 si la connexion timeout
  // côté serveur (Gmail ferme les sessions inactives après quelques minutes)
  Serial.println(F("[Mail] Trace 8: Fermeture session SMTP..."));
  _smtp.closeSession();
  _ready = false;
  Serial.println(F("[Mail] ✅ Session SMTP fermée proprement"));

  // Libérer le mutex TLS (CRITIQUE - doit être fait après fermeture session)
  TLSMutex::release();
  
  return ok;
}
#else
void Mailer::setPowerManager(PowerManager*) {}
bool Mailer::begin() { Serial.println("[Mail] Désactivé (FEATURE_MAIL=0)"); return true; }
bool Mailer::sendSync(const char*, const char*, const char*, const char*, N3Severity) { return false; }
bool Mailer::send(const char*, const char*, const char*, const char*) { return false; }
bool Mailer::sendAlert(const char* subject, const char* message, const char* toEmail, bool includeDetailedReport) {
  (void)subject; (void)message; (void)toEmail; (void)includeDetailedReport; return false;
}
bool Mailer::sendAlertSync(const char* subject, const char* message, const char* toEmail, bool includeDetailedReport) {
  (void)subject; (void)message; (void)toEmail; (void)includeDetailedReport; return false;
}
bool Mailer::sendAlertAcked(const char* subject, const char* message, const char* toEmail,
                            bool includeDetailedReport, bool* ackFlag, bool ackFailValue) {
  (void)subject; (void)message; (void)toEmail; (void)includeDetailedReport;
  (void)ackFlag; (void)ackFailValue; return false;
}
bool Mailer::enqueueAlert(const char* subject, const char* message, const char* toEmail,
                          bool includeDetailedReport, bool* ackFlag, bool ackFailValue) {
  (void)subject; (void)message; (void)toEmail; (void)includeDetailedReport;
  (void)ackFlag; (void)ackFailValue; return false;
}
bool Mailer::sendSleepMail(const char* reason, uint32_t sleepDurationSeconds, const SensorReadings& readings,
                           const char* toEmail) {
  (void)reason; (void)sleepDurationSeconds; (void)readings; (void)toEmail; return false;
}
bool Mailer::sendWakeMail(const char* reason, uint32_t actualSleepSeconds, const SensorReadings& readings,
                          const char* toEmail) {
  (void)reason; (void)actualSleepSeconds; (void)readings; (void)toEmail; return false;
}
bool Mailer::initMailQueue() { return true; }
bool Mailer::processOneMailSync() { return false; }
bool Mailer::hasPendingMails() const { return false; }
uint32_t Mailer::getQueuedMails() const { return 0; }
#endif

#if FEATURE_MAIL && FEATURE_MAIL != 0
bool Mailer::sendAlertSync(const char* subject, const char* message, const char* toEmail, bool includeDetailedReport) {
  Serial.println(F("[Mail] ===== DIAGNOSTIC SENDALERT ====="));
  Serial.printf("[Mail] _ready: %s\n", _ready ? "TRUE" : "FALSE");
  Serial.printf("[Mail] subject: '%s'\n", subject ? subject : "NULL");
  size_t msgLen = message ? strlen(message) : 0;
  Serial.printf("[Mail] message length: %d\n", msgLen);
  Serial.printf("[Mail] toEmail: '%s'\n", toEmail ? toEmail : "NULL");
  Serial.printf("[Mail] includeDetailedReport: %s\n", includeDetailedReport ? "true" : "false");
  
  // Vérifications préalables
  if (!subject) {
    Serial.println(F("[Mail] ❌ ERREUR: subject est NULL"));
    return false;
  }
  if (!message || msgLen == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: message vide"));
    return false;
  }
  
  // Utiliser fallback si toEmail vide (cohérent avec send() et sendAlert())
  const char* targetEmail = toEmail;
  if (!targetEmail || strlen(targetEmail) == 0) {
    Serial.println(F("[Mail] ⚠️ toEmail vide, utilisation DEFAULT_RECIPIENT"));
    targetEmail = EmailConfig::DEFAULT_RECIPIENT;
  }
  
  // Sujet transmis brut à sendSync qui applique le préfixe "[FFP5][Pn]".
  // (Ne plus préfixer "FFP5CS - " ici pour éviter un double habillage.)
  static char alertSubject[128];
  snprintf(alertSubject, sizeof(alertSubject), "%s", subject);
  Serial.printf("[Mail] alertSubject créer: '%s'\n", alertSubject);
  
  // Piste 4: utiliser le buffer partagé s_mailMessageBuffer (sendSync ne recopie pas si même pointeur)
  size_t offset = 0;
  size_t remaining = sizeof(s_mailMessageBuffer);
  int written = 0;
  
  // Copier le message initial
  size_t initialMsgLen = msgLen;
  if (initialMsgLen >= remaining) {
    initialMsgLen = remaining - 1;
  }
  strncpy(s_mailMessageBuffer, message, initialMsgLen);
  s_mailMessageBuffer[initialMsgLen] = '\0';
  offset = initialMsgLen;
  remaining -= initialMsgLen;
  Serial.printf("[Mail] s_mailMessageBuffer initial: %d chars\n", offset);
  
  // Rapport temporel détaillé uniquement pour alertes diagnostic (boot, OTA, panic)
  static Diagnostics tempDiag;
  tempDiag.loadFromNvs();
  bool isCritical = tempDiag.hasPanicInfo() || tempDiag.hasCrashInfo();
  if (includeDetailedReport) {
    const char* timeReport = buildDetailedTimeReport(tempDiag);
    if (!timeReport) {
      Serial.println(F("[Mail] ❌ buildDetailedTimeReport returned NULL"));
      return false;
    }
    if (remaining > 0) {
      written = snprintf(s_mailMessageBuffer + offset, remaining, "%s", timeReport);
      if (written > 0 && (size_t)written < remaining) {
        offset += written;
        remaining -= written;
      } else {
        s_mailMessageBuffer[sizeof(s_mailMessageBuffer) - 1] = '\0';
        remaining = 0;
      }
    }
    Serial.printf("[Mail] s_mailMessageBuffer après timeReport: %d chars\n", strlen(s_mailMessageBuffer));
  }
  
  Serial.println(F("[Mail] ===== ENVOI D'ALERTE (SYNC) ====="));
  Serial.printf("[Mail] Type: %s\n", includeDetailedReport ? "diagnostic" : "opérationnel");
  Serial.printf("[Mail] Destinataire: %s\n", targetEmail);
  Serial.printf("[Mail] Objet final: %s\n", alertSubject);
  Serial.println(F("[Mail] ==========================="));

  // Marqueur footer complet (alerte critique + rapport détaillé déjà inclus) - désactivé pour l'instant
  if (false && isCritical && remaining > 0) {
    const char* marker = "\n[Footer complet déjà inclus]";
    written = snprintf(s_mailMessageBuffer + offset, remaining, "%s", marker);
    if (written > 0 && (size_t)written < remaining) {
      offset += written;
      remaining -= written;
    }
  }
  
  // Sévérité : panic/crash = P1 Critical ; boot/OTA (rapport détaillé) = P4
  // Diagnostic ; alerte opérationnelle = P2 Alert. Filtrée par le mode dans sendSync.
  N3Severity alertSeverity = isCritical
                                 ? N3Severity::Critical
                                 : (includeDetailedReport ? N3Severity::Diagnostic : N3Severity::Alert);
  bool result = sendSync(alertSubject, s_mailMessageBuffer, "User", targetEmail, alertSeverity);
  Serial.printf("[Mail] ===== RÉSULTAT SENDALERTSYNC: %s =====\n", result ? "SUCCESS" : "FAILED");
  
  if (result && tempDiag.hasPanicInfo()) {
    extern Diagnostics diag;
    diag.clearPanicInfoAfterReport();
    Serial.println(F("[Mail] ✅ Infos PANIC nettoyées après envoi du mail"));
  }
  
  return result;
}

bool Mailer::sendSleepMail(const char* reason, uint32_t sleepDurationSeconds, const SensorReadings& readings,
                           const char* toEmail) {
  const char* dest = (toEmail && strlen(toEmail) > 0) ? toEmail : EmailConfig::DEFAULT_RECIPIENT;
  // Buffers partagés sleep/wake (référence tableau : sizeof conservé). Voir g_sleepWake* en tête de fichier.
  char (&sleepSubject)[64] = g_sleepWakeSubject;
  snprintf(sleepSubject, sizeof(sleepSubject), "Mise en veille");
  
  char (&sleepMessage)[1024] = g_sleepWakeMessage;
  size_t offset = 0;
  size_t remaining = sizeof(sleepMessage);
  int written = 0;
  
  // Construire le message avec snprintf()
  written = snprintf(sleepMessage + offset, remaining,
    "Le système FFP5CS entre en veille légère\n\n"
    "-- INFORMATIONS DE MISE EN VEILLE --\n"
    "Raison: %s\n"
    "Durée prévue: %u secondes\n"
    "Timestamp: ",
    reason ? reason : "N/A", sleepDurationSeconds);
  if (written < 0 || (size_t)written >= remaining) {
    sleepMessage[sizeof(sleepMessage) - 1] = '\0';
    return false;
  }
  offset += written;
  remaining -= written;
  
  // Ajouter l'heure actuelle
  time_t now = getSafeEpochForDisplay();
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    written = snprintf(sleepMessage + offset, remaining, "%s\n", timeBuf);
  } else {
    written = snprintf(sleepMessage + offset, remaining, "Erreur heure\n");
  }
  if (written < 0 || (size_t)written >= remaining) {
    sleepMessage[sizeof(sleepMessage) - 1] = '\0';
    return false;
  }
  offset += written;
  remaining -= written;
  
  // Ajouter les informations système détaillées
  // UTILISE LES DERNIÈRES LECTURES (passées en paramètre) au lieu de relire les capteurs
  extern SystemActuators acts;
  written = snprintf(sleepMessage + offset, remaining,
    "\n-- ÉTAT SYSTÈME AVANT VEILLE --\n"
    "- Temp eau: %.1f °C\n"
    "- Temp air: %.1f °C\n"
    "- Aqua lvl: %u cm\n"
    "- Réserve lvl: %u cm\n"
    "- Pompe aquarium: %s\n"
    "- Pompe réservoir: %s\n"
    "- Chauffage: %s\n"
    "- Lumière: %s\n"
    "\n-- Configuration Sleep --\n"
    "- Délai d'activité: Configuration adaptative\n"
    "- Mode adaptatif: ACTIF\n",
    readings.tempWater, readings.tempAir, readings.wlAqua, readings.wlTank,
    acts.isAquaPumpRunning() ? "ON" : "OFF",
    acts.isTankPumpRunning() ? "ON" : "OFF",
    acts.isHeaterOn() ? "ON" : "OFF",
    acts.isLightOn() ? "ON" : "OFF");
  if (written < 0 || (size_t)written >= remaining) {
    sleepMessage[sizeof(sleepMessage) - 1] = '\0';
  }
  
  Serial.println(F("[Mail] ===== ENVOI MAIL VEILLE ====="));
  Serial.printf("[Mail] Raison: %s\n", reason);
  Serial.printf("[Mail] Durée: %u s\n", sleepDurationSeconds);
  Serial.println(F("[Mail] ⚡ Utilisation des dernières lectures (pas de nouvelle lecture capteurs)"));
  Serial.println(F("[Mail] =============================="));
  
  return sendSync(sleepSubject, sleepMessage, "User", dest, N3Severity::Diagnostic);
}

bool Mailer::sendWakeMail(const char* reason, uint32_t actualSleepSeconds, const SensorReadings& readings,
                         const char* toEmail) {
  const char* dest = (toEmail && strlen(toEmail) > 0) ? toEmail : EmailConfig::DEFAULT_RECIPIENT;
  // Buffers partagés sleep/wake (référence tableau : sizeof conservé). Voir g_sleepWake* en tête de fichier.
  char (&wakeSubject)[64] = g_sleepWakeSubject;
  snprintf(wakeSubject, sizeof(wakeSubject), "Réveil du système");
  
  char (&wakeMessage)[1024] = g_sleepWakeMessage;
  size_t offset = 0;
  size_t remaining = sizeof(wakeMessage);
  int written = 0;
  
  // Construire le message avec snprintf()
  written = snprintf(wakeMessage + offset, remaining,
    "Le système FFP5CS s'est réveillé de sa veille légère\n\n"
    "-- INFORMATIONS DE RÉVEIL --\n"
    "Raison: %s\n"
    "Durée réelle de veille: %u secondes\n"
    "Timestamp: ",
    reason ? reason : "N/A", actualSleepSeconds);
  if (written < 0 || (size_t)written >= remaining) {
    wakeMessage[sizeof(wakeMessage) - 1] = '\0';
    return false;
  }
  offset += written;
  remaining -= written;
  
  // Ajouter l'heure actuelle
  time_t now = getSafeEpochForDisplay();
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    written = snprintf(wakeMessage + offset, remaining, "%s\n", timeBuf);
  } else {
    written = snprintf(wakeMessage + offset, remaining, "Erreur heure\n");
  }
  if (written < 0 || (size_t)written >= remaining) {
    wakeMessage[sizeof(wakeMessage) - 1] = '\0';
    return false;
  }
  offset += written;
  remaining -= written;
  
  // Ajouter les informations système détaillées
  // UTILISE LES DERNIÈRES LECTURES (passées en paramètre) au lieu de relire les capteurs
  extern SystemActuators acts;
  written = snprintf(wakeMessage + offset, remaining,
    "\n-- ÉTAT SYSTÈME AU RÉVEIL --\n"
    "- Temp eau: %.1f °C\n"
    "- Temp air: %.1f °C\n"
    "- Aqua lvl: %u cm\n"
    "- Réserve lvl: %u cm\n"
    "- Pompe aquarium: %s\n"
    "- Pompe réservoir: %s\n"
    "- Chauffage: %s\n"
    "- Lumière: %s\n"
    "\n-- Configuration Sleep --\n"
    "- Délai d'activité: Configuration adaptative\n"
    "- Mode adaptatif: ACTIF\n"
    "\n-- CONNEXION RÉSEAU --\n",
    readings.tempWater, readings.tempAir, readings.wlAqua, readings.wlTank,
    acts.isAquaPumpRunning() ? "ON" : "OFF",
    acts.isTankPumpRunning() ? "ON" : "OFF",
    acts.isHeaterOn() ? "ON" : "OFF",
    acts.isLightOn() ? "ON" : "OFF");
  if (written < 0 || (size_t)written >= remaining) {
    wakeMessage[sizeof(wakeMessage) - 1] = '\0';
    return false;
  }
  offset += written;
  remaining -= written;
  
  // Ajouter les informations de connexion WiFi
  if (WiFi.status() == WL_CONNECTED) {
    char ssid[33];
    char ip[16];
    WiFiHelpers::getSSID(ssid, sizeof(ssid));
    WiFiHelpers::getIPString(ip, sizeof(ip));
    written = snprintf(wakeMessage + offset, remaining,
      "- WiFi: Connecté\n"
      "- SSID: %s\n"
      "- IP: %s\n"
      "- RSSI: %d dBm\n",
      ssid, ip, WiFi.RSSI());
  } else {
    written = snprintf(wakeMessage + offset, remaining, "- WiFi: Déconnecté\n");
  }
  if (written < 0 || (size_t)written >= remaining) {
    wakeMessage[sizeof(wakeMessage) - 1] = '\0';
  }
  
  Serial.println(F("[Mail] ===== ENVOI MAIL RÉVEIL ====="));
  Serial.printf("[Mail] Raison: %s\n", reason);
  Serial.printf("[Mail] Durée veille: %u s\n", actualSleepSeconds);
  Serial.println(F("[Mail] =============================="));
  
  return sendSync(wakeSubject, wakeMessage, "User", dest, N3Severity::Diagnostic);
}

#endif

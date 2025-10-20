// Mailer implementation can be compiled out to save flash on ESP32 WROOM
#include "mailer.h"
#include "project_config.h"
#include "system_sensors.h"
#include "system_actuators.h"
#include "diagnostics.h"
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include <LittleFS.h>

#if FEATURE_MAIL && FEATURE_MAIL != 0

// Construit un bloc d'informations système (réseau, version, mémoire, uptime)
static String formatUptime(unsigned long ms) {
  unsigned long totalSec = ms / 1000UL;
  unsigned int days = totalSec / 86400UL;
  totalSec %= 86400UL;
  unsigned int hours = totalSec / 3600UL;
  totalSec %= 3600UL;
  unsigned int mins = totalSec / 60UL;
  unsigned int secs = totalSec % 60UL;
  char buf[48];
  snprintf(buf, sizeof(buf), "%ud %02u:%02u:%02u", days, hours, mins, secs);
  return String(buf);
}

static String buildSystemInfoFooter() {
  String footer;
  footer.reserve(2048);
  footer += "\n\n-- Infos système --\n";

  // Version / carte
#ifdef BOARD_S3
  const char* board = "ESP32-S3";
#else
  const char* board = "ESP32";
#endif
  footer += "- Version: "; footer += Config::VERSION; footer += "\n";
  footer += "- Carte: "; footer += board; footer += "\n";
  footer += "- Environnement: "; footer += CompatibilityUtils::getEnvironmentName(); footer += "\n";

  // Réseau
  bool connected = WiFi.isConnected();
  String ssid = connected ? WiFi.SSID() : String("(déconnecté)");
  String ip = connected ? WiFi.localIP().toString() : String("-");
  long rssi = connected ? WiFi.RSSI() : 0;
  String mac = WiFi.macAddress();
  footer += "- SSID: "; footer += ssid; footer += "\n";
  footer += "- IP: "; footer += ip; footer += "\n";
  footer += "- RSSI: "; footer += String(rssi); footer += " dBm\n";
  footer += "- MAC: "; footer += mac; footer += "\n";

  // Qualité WiFi (%), canal, BSSID, hostname, GW, masque, DNS
  int wifiQuality = 0;
  if (connected) {
    if (rssi <= -100) wifiQuality = 0; else if (rssi >= -50) wifiQuality = 100; else wifiQuality = 2 * (rssi + 100);
    footer += "- Qualité: "; footer += String(wifiQuality); footer += "%\n";
    footer += "- Canal: "; footer += String(WiFi.channel()); footer += "\n";
    footer += "- BSSID: "; footer += WiFi.BSSIDstr(); footer += "\n";
    const char* host = WiFi.getHostname();
    if (host && *host) { footer += "- Hostname: "; footer += host; footer += "\n"; }
    footer += "- Passerelle: "; footer += WiFi.gatewayIP().toString(); footer += "\n";
    footer += "- Masque: "; footer += WiFi.subnetMask().toString(); footer += "\n";
    footer += "- DNS: "; footer += WiFi.dnsIP(0).toString(); footer += "\n";
  }

  // Mode WiFi et AP si actif
  wifi_mode_t mode = WiFi.getMode();
  const char* modeStr = (mode == WIFI_OFF ? "OFF" : mode == WIFI_STA ? "STA" : mode == WIFI_AP ? "AP" : mode == WIFI_AP_STA ? "AP+STA" : "UNKNOWN");
  footer += "- WiFi mode: "; footer += modeStr; footer += "\n";
  if (mode == WIFI_AP || mode == WIFI_AP_STA) {
    footer += "- AP SSID: "; footer += WiFi.softAPSSID(); footer += "\n";
    footer += "- AP IP: "; footer += WiFi.softAPIP().toString(); footer += "\n";
  }

  // ======================
  // INFORMATIONS TEMPORELLES DÉTAILLÉES
  // ======================
  footer += "\n-- Informations temporelles --\n";
  time_t now = time(nullptr);
  footer += "- Epoch: "; footer += String((unsigned long)now); footer += "\n";
  if (now > 100000) {
    struct tm tmInfo;
    localtime_r(&now, &tmInfo);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmInfo);
    footer += "- Local time: "; footer += tbuf; footer += "\n";
    footer += "- Jour de la semaine: "; footer += String(tmInfo.tm_wday); footer += " (0=dimanche)\n";
    footer += "- Jour de l'année: "; footer += String(tmInfo.tm_yday); footer += "\n";
    footer += "- DST actif: "; footer += (tmInfo.tm_isdst ? "OUI" : "NON"); footer += "\n";
    footer += "- Timezone: Maroc UTC+1\n";
  }
  
  // Informations NTP
  footer += "- NTP Server: "; footer += SystemConfig::NTP_SERVER; footer += "\n";
  footer += "- GMT Offset: +"; footer += String(SystemConfig::NTP_GMT_OFFSET_SEC/3600); footer += "h\n";
  footer += "- DST Offset: +"; footer += String(SystemConfig::NTP_DAYLIGHT_OFFSET_SEC/3600); footer += "h\n";
  
  // Informations de dérive temporelle si disponibles
  // extern TimeDriftMonitor timeDriftMonitor;
  // if (timeDriftMonitor.isDriftCalculated()) {
  //   footer += "- Dérive PPM: "; footer += String(timeDriftMonitor.getDriftPPM(), 2); footer += "\n";
  //   footer += "- Dérive secondes: "; footer += String(timeDriftMonitor.getDriftSeconds(), 2); footer += "\n";
  //   time_t lastSync = timeDriftMonitor.getLastSyncTime();
  //   if (lastSync > 0) {
  //     struct tm syncInfo;
  //     localtime_r(&lastSync, &syncInfo);
  //     char syncBuf[32];
  //     strftime(syncBuf, sizeof(syncBuf), "%Y-%m-%d %H:%M:%S", &syncInfo);
  //     footer += "- Dernière sync NTP: "; footer += syncBuf; footer += "\n";
  //   }
  // } else {
  //   footer += "- Dérive: Non calculée\n";
  // }
  
  // Informations RTC/Flash
  Preferences prefs;
  prefs.begin("rtc", true);
  unsigned long savedEpoch = prefs.getULong("epoch", 0);
  prefs.end();
  if (savedEpoch > 0) {
    footer += "- RTC Flash epoch: "; footer += String(savedEpoch); footer += "\n";
    if (savedEpoch != (unsigned long)now) {
      long diff = (long)now - (long)savedEpoch;
      footer += "- Diff RTC vs actuel: "; footer += String(diff); footer += " secondes\n";
    }
  }

  // Mémoire
  footer += "- Heap free: "; footer += String(ESP.getFreeHeap()); footer += " bytes\n";
  footer += "- Heap min free: "; footer += String(ESP.getMinFreeHeap()); footer += " bytes\n";
  footer += "- Heap max alloc: "; footer += String(ESP.getMaxAllocHeap()); footer += " bytes\n";
  size_t psram = ESP.getPsramSize();
  if (psram > 0) {
    footer += "- PSRAM size: "; footer += String(psram); footer += " bytes\n";
    footer += "- PSRAM free: "; footer += String(ESP.getFreePsram()); footer += " bytes\n";
  }

  // Diagnostics persistés si disponibles
  prefs.begin("diagnostics", true);
  unsigned int rebootCnt = prefs.getUInt("rebootCnt", 0);
  unsigned int minHeap   = prefs.getUInt("minHeap", 0);
  prefs.end();
  footer += "- Reboots: "; footer += String(rebootCnt); footer += "\n";
  if (minHeap > 0) { footer += "- Min heap: "; footer += String(minHeap); footer += " bytes\n"; }

  // Uptime
  footer += "- Uptime: "; footer += formatUptime(millis()); footer += "\n";

  // ======================
  // Mesures & Actionneurs
  // ======================
  footer += "\n-- Mesures / Actionneurs --\n";
  extern SystemSensors sensors;
  extern SystemActuators acts;
  // Lecture instantanée (peut prendre quelques centaines de ms)
  SensorReadings rs = sensors.read();
  int diffM = sensors.diffMaree(rs.wlAqua);
  footer += "- Temp eau: "; footer += String(rs.tempWater, 1); footer += " °C\n";
  footer += "- Temp air: "; footer += String(rs.tempAir, 1); footer += " °C\n";
  footer += "- Humidité: "; footer += String(rs.humidity, 0); footer += " %\n";
  footer += "- Aqua lvl: "; footer += String(rs.wlAqua); footer += " cm\n";
  footer += "- Réserve lvl: "; footer += String(rs.wlTank); footer += " cm\n";
  footer += "- Potager lvl: "; footer += String(rs.wlPota); footer += " cm\n";
  footer += "- Luminosité: "; footer += String(rs.luminosite); footer += "\n";
  footer += "- Diff marée: "; footer += String(diffM); footer += "\n";
  
  // Ajouter les informations de délai d'activité
  footer += "\n-- Configuration Sleep --\n";
  // Note: autoCtrl n'est pas accessible ici, utiliser des valeurs par défaut
  footer += "- Délai d'activité: Configuration adaptative\n";
  footer += "- Mode adaptatif: ACTIF\n";
  
  // États actionneurs
  footer += String("- Pompe aquarium: ") + (acts.isAquaPumpRunning()?"ON":"OFF") + "\n";
  footer += String("- Pompe réservoir: ") + (acts.isTankPumpRunning()?"ON":"OFF") + "\n";
  footer += String("- Chauffage: ") + (acts.isHeaterOn()?"ON":"OFF") + "\n";
  footer += String("- Lumière: ") + (acts.isLightOn()?"ON":"OFF") + "\n";
  // Statistiques pompe réservoir
  unsigned long curRun = acts.getTankPumpCurrentRuntime();
  footer += "- Pompe réservoir runtime courant: "; footer += String(curRun); footer += " ms\n";
  footer += "- Pompe réservoir runtime total: "; footer += String(acts.getTankPumpTotalRuntime()); footer += " ms\n";
  footer += "- Pompe réservoir arrêts totaux: "; footer += String(acts.getTankPumpTotalStops()); footer += "\n";

  // CPU / Chip / SDK
  footer += "- CPU freq: "; footer += String(getCpuFrequencyMhz()); footer += " MHz\n";
  footer += "- Chip: "; footer += ESP.getChipModel(); footer += ", rev "; footer += String(ESP.getChipRevision()); footer += ", cores "; footer += String(ESP.getChipCores()); footer += "\n";
  footer += "- SDK: "; footer += ESP.getSdkVersion(); footer += "\n";

  // Sketch / flash
  footer += "- Sketch size: "; footer += String(ESP.getSketchSize()); footer += " bytes\n";
  footer += "- Free sketch: "; footer += String(ESP.getFreeSketchSpace()); footer += " bytes\n";
  footer += "- Flash size: "; footer += String(ESP.getFlashChipSize()); footer += " bytes\n";

  // Filesystem (LittleFS)
  if (LittleFS.begin(false)) {
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    footer += "- FS LittleFS: "; footer += String(used); footer += "/"; footer += String(total); footer += " bytes\n";
    // Ne pas démonter LittleFS ici pour éviter les erreurs de montage dans d'autres modules
  }

  // Endpoints serveur utiles
  footer += "- POST URL: "; footer += Config::SERVER_POST_DATA; footer += "\n";
  footer += "- OUTPUT URL: "; footer += Config::SERVER_OUTPUT; footer += "\n";

  // ======================
  // NVS principales (resume)
  // ======================
  footer += "\n-- NVS principales --\n";
  {
    Preferences prefs;
    // Namespace bouffe
    prefs.begin("bouffe", true);
    footer += "- bouffeMatinOk: "; footer += prefs.getBool("bouffeMatinOk", false) ? "true" : "false"; footer += "\n";
    footer += "- bouffeMidiOk: ";  footer += prefs.getBool("bouffeMidiOk", false)  ? "true" : "false"; footer += "\n";
    footer += "- bouffeSoirOk: ";  footer += prefs.getBool("bouffeSoirOk", false)  ? "true" : "false"; footer += "\n";
    footer += "- lastJourBouf: ";  footer += String(prefs.getInt("lastJourBouf", -1)); footer += "\n";
    footer += "- pompeAquaLocked: "; footer += prefs.getBool("pompeAquaLocked", false) ? "true" : "false"; footer += "\n";
    footer += "- forceWakeUp: "; footer += prefs.getBool("forceWakeUp", false) ? "true" : "false"; footer += "\n";
    prefs.end();

    // Namespace ota
    prefs.begin("ota", true);
    footer += "- ota.updateFlag: "; footer += prefs.getBool("updateFlag", true) ? "true" : "false"; footer += "\n";
    prefs.end();

    // Namespace remoteVars (aperçu)
    prefs.begin("remoteVars", true);
    String remoteJson = prefs.getString("json", "");
    prefs.end();
    if (remoteJson.length() > 0) {
      String preview = remoteJson.substring(0, min((size_t)200, (size_t)remoteJson.length()));
      footer += "- remoteVars.json: ";
      footer += preview;
      if (remoteJson.length() > 200) footer += "...";
      footer += "\n";
    } else {
      footer += "- remoteVars.json: (vide)\n";
    }

    // Namespace rtc
    prefs.begin("rtc", true);
    unsigned long savedEpoch = prefs.getULong("epoch", 0);
    prefs.end();
    footer += "- rtc.epoch: "; footer += String(savedEpoch); footer += "\n";

    // Namespace diagnostics (déjà partiellement inclus plus haut, rappel condensé)
    prefs.begin("diagnostics", true);
    unsigned int rebootCnt2 = prefs.getUInt("rebootCnt", 0);
    unsigned int minHeap2   = prefs.getUInt("minHeap", 0);
    prefs.end();
    footer += "- diagnostics.rebootCnt: "; footer += String(rebootCnt2); footer += "\n";
    footer += "- diagnostics.minHeap: "; footer += String(minHeap2); footer += "\n";
  }

  return footer;
}

// Fonction pour générer un rapport temporel détaillé
static String buildDetailedTimeReport(const Diagnostics& diagnostics) {
  String report;
  report.reserve(1024);
  report += "\n\n-- RAPPORT TEMPOREL DÉTAILLÉ --\n";
  
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    report += "Heure actuelle: "; report += timeBuf; report += "\n";
    report += "Epoch: "; report += String((unsigned long)now); report += "\n";
    report += "Jour de la semaine: "; report += String(timeinfo.tm_wday); report += " (0=dimanche)\n";
    report += "Jour de l'année: "; report += String(timeinfo.tm_yday); report += "\n";
    report += "DST actif: "; report += (timeinfo.tm_isdst ? "OUI" : "NON"); report += "\n";
    
    // Informations sur le temps de fonctionnement
    unsigned long uptimeMs = millis();
    unsigned long uptimeSec = uptimeMs / 1000;
    unsigned long uptimeMin = uptimeSec / 60;
    unsigned long uptimeHour = uptimeMin / 60;
    unsigned long uptimeDay = uptimeHour / 24;
    
    report += "Uptime: ";
    if (uptimeDay > 0) report += String(uptimeDay) + "j ";
    if (uptimeHour % 24 > 0) report += String(uptimeHour % 24) + "h ";
    if (uptimeMin % 60 > 0) report += String(uptimeMin % 60) + "m ";
    report += String(uptimeSec % 60) + "s\n";
    
    // Informations NTP
    report += "Serveur NTP: "; report += SystemConfig::NTP_SERVER; report += "\n";
    report += "GMT Offset: +"; report += String(SystemConfig::NTP_GMT_OFFSET_SEC/3600); report += "h\n";
    report += "DST Offset: +"; report += String(SystemConfig::NTP_DAYLIGHT_OFFSET_SEC/3600); report += "h\n";
    
    // Informations de dérive si disponibles
    // extern TimeDriftMonitor timeDriftMonitor;
    // if (timeDriftMonitor.isDriftCalculated()) {
    //   report += "Dérive PPM: "; report += String(timeDriftMonitor.getDriftPPM(), 3); report += "\n";
    //   report += "Dérive secondes: "; report += String(timeDriftMonitor.getDriftSeconds(), 3); report += "\n";
    //   time_t lastSync = timeDriftMonitor.getLastSyncTime();
    //   if (lastSync > 0) {
    //     struct tm syncInfo;
    //     localtime_r(&lastSync, &syncInfo);
    //     char syncBuf[32];
    //     strftime(syncBuf, sizeof(syncBuf), "%Y-%m-%d %H:%M:%S", &syncInfo);
    //     report += "Dernière sync NTP: "; report += syncBuf; report += "\n";
    //     
    //     // Calcul du temps écoulé depuis la dernière sync
    //     time_t timeSinceSync = now - lastSync;
    //     if (timeSinceSync < 3600) {
    //       report += "Temps depuis sync: "; report += String(timeSinceSync/60); report += " minutes\n";
    //     } else if (timeSinceSync < 86400) {
    //       report += "Temps depuis sync: "; report += String(timeSinceSync/3600); report += " heures\n";
    //     } else {
    //       report += "Temps depuis sync: "; report += String(timeSinceSync/86400); report += " jours\n";
    //     }
    //   }
    // } else {
    //   report += "Dérive: Non calculée\n";
    // }
    
    // Informations RTC/Flash
    Preferences prefs;
    prefs.begin("rtc", true);
    unsigned long savedEpoch = prefs.getULong("epoch", 0);
    prefs.end();
    if (savedEpoch > 0) {
      report += "RTC Flash epoch: "; report += String(savedEpoch); report += "\n";
      if (savedEpoch != (unsigned long)now) {
        long diff = (long)now - (long)savedEpoch;
        report += "Diff RTC vs actuel: "; report += String(diff); report += " secondes\n";
        if (abs(diff) > 60) {
          report += "⚠️ Écart important entre RTC et temps actuel!\n";
        }
      }
    }
  } else {
    report += "Erreur: Impossible de récupérer l'heure locale\n";
  }
  
  // Ajouter les informations de redémarrage
  report += "\n-- INFORMATIONS DE REDÉMARRAGE --\n";
  report += diagnostics.generateRestartReport();
  
  return report;
}

bool Mailer::begin() {
  Serial.println(F("[Mail] ===== INITIALISATION MAILER ====="));
  
  // Prépare uniquement la configuration SMTP.
  // La connexion TLS/SMTP sera établie à la première utilisation dans send().

  _cfg = Session_Config();
  _cfg.server.host_name = Config::SMTP_HOST;
  _cfg.server.port      = Config::SMTP_PORT_SSL;
  _cfg.login.email      = Secrets::AUTHOR_EMAIL;
  _cfg.login.password   = Secrets::AUTHOR_PASSWORD;

  // Diagnostic de la configuration
  Serial.printf("[Mail] SMTP_HOST: '%s'\n", Config::SMTP_HOST);
  Serial.printf("[Mail] SMTP_PORT: %d\n", Config::SMTP_PORT_SSL);
  Serial.printf("[Mail] AUTHOR_EMAIL: '%s'\n", Secrets::AUTHOR_EMAIL);
  Serial.printf("[Mail] AUTHOR_PASSWORD length: %d\n", strlen(Secrets::AUTHOR_PASSWORD));
  
  // Vérifications de configuration
  if (!Config::SMTP_HOST || strlen(Config::SMTP_HOST) == 0) {
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

bool Mailer::send(const char* subject, const char* message, const char* toName, const char* toEmail) {
  Serial.println(F("[Mail] ===== DIAGNOSTIC SEND ====="));
  Serial.printf("[Mail] _ready: %s\n", _ready ? "TRUE" : "FALSE");
  Serial.printf("[Mail] _smtp.connected(): %s\n", _smtp.connected() ? "TRUE" : "FALSE");
  
  if (!_ready || !_smtp.connected()) {
    Serial.println(F("[Mail] ⚠️ Connexion SMTP requise"));
    // tentative de reconnexion
    _smtp.closeSession();
    Serial.println(F("[Mail] Tentative de connexion SMTP..."));
    _ready = _smtp.connect(&_cfg);
    if(!_ready){
      Serial.printf("[Mail] ❌ Reconnexion SMTP échouée - code: %d\n", _smtp.statusCode());
      Serial.printf("[Mail] ❌ Erreur: %s\n", _smtp.errorReason().c_str());
      return false;
    } else {
      Serial.println(F("[Mail] ✅ Connexion SMTP réussie"));
    }
  } else {
    Serial.println(F("[Mail] ✅ Connexion SMTP déjà active"));
  }
  
  // FIX: Utiliser des buffers statiques pour éviter les dangling pointers
  // La bibliothèque ESP Mail Client peut utiliser les pointeurs de manière asynchrone
  static char fromNameBuf[64];
  static char subjectBuf[128];
  static String finalMessageStatic;  // String statique pour persistance
  
  // Construire l'objet avec l'environnement de manière explicite
  const char* envName = CompatibilityUtils::getEnvironmentName();
  
  // Construction du nom d'expéditeur dans un buffer statique
  snprintf(fromNameBuf, sizeof(fromNameBuf), "FFP3 [%s]", envName ? envName : "");
  
  // Construction du sujet dans un buffer statique
  snprintf(subjectBuf, sizeof(subjectBuf), "[%s] %s", envName ? envName : "", subject ? subject : "");
  
  // Construction du message final (String statique pour persistance)
  finalMessageStatic = message ? message : "";
  finalMessageStatic += buildSystemInfoFooter();
  
  // Configuration du message SMTP
  SMTP_Message msg;
  msg.sender.name  = fromNameBuf;
  msg.sender.email = Secrets::AUTHOR_EMAIL;
  msg.subject      = subjectBuf;
  msg.addRecipient(toName, toEmail);
  msg.text.content = finalMessageStatic.c_str();
  
  // Affichage des détails du mail avant envoi avec informations temporelles
  time_t mailTime = time(nullptr);
  struct tm mailTimeInfo;
  localtime_r(&mailTime, &mailTimeInfo);
  char mailTimeBuf[32];
  strftime(mailTimeBuf, sizeof(mailTimeBuf), "%Y-%m-%d %H:%M:%S", &mailTimeInfo);
  
  Serial.println(F("[Mail] ===== DÉTAILS DU MAIL ====="));
  Serial.printf("[Mail] Heure d'envoi: %s (epoch: %lu)\n", mailTimeBuf, mailTime);
  Serial.printf("[Mail] De: %s <%s>\n", msg.sender.name, msg.sender.email);
  Serial.printf("[Mail] À: %s <%s>\n", toName, toEmail);
  Serial.printf("[Mail] Objet: %s\n", msg.subject);
  Serial.println(F("[Mail] Contenu:"));
  Serial.println(finalMessageStatic);
  Serial.println(F("[Mail] ==========================="));
  
  bool ok = MailClient.sendMail(&_smtp, &msg);
  if (!ok) {
    Serial.printf("[Mail] ERREUR sendMail code=%d err=%d reason=%s\n", _smtp.statusCode(), _smtp.errorCode(), _smtp.errorReason().c_str());
  } else {
    Serial.println(F("[Mail] Message envoyé avec succès ✔"));
  }
  return ok;
}
#else
bool Mailer::begin() { Serial.println("[Mail] Désactivé (FEATURE_MAIL=0)"); return true; }
bool Mailer::send(const char*, const char*, const char*, const char*) { return false; }
bool Mailer::sendAlert(const char* subject, const String& message, const char* toEmail) {
  (void)subject; (void)message; (void)toEmail; return false;
}
bool Mailer::sendSleepMail(const char* reason, uint32_t sleepDurationSeconds, const SensorReadings& readings) {
  (void)reason; (void)sleepDurationSeconds; (void)readings; return false;
}
bool Mailer::sendWakeMail(const char* reason, uint32_t actualSleepSeconds, const SensorReadings& readings) {
  (void)reason; (void)actualSleepSeconds; (void)readings; return false;
}
#endif

#if FEATURE_MAIL && FEATURE_MAIL != 0
bool Mailer::sendAlert(const char* subject, const String& message, const char* toEmail) {
  Serial.println(F("[Mail] ===== DIAGNOSTIC SENDALERT ====="));
  Serial.printf("[Mail] _ready: %s\n", _ready ? "TRUE" : "FALSE");
  Serial.printf("[Mail] subject: '%s'\n", subject ? subject : "NULL");
  Serial.printf("[Mail] message length: %d\n", message.length());
  Serial.printf("[Mail] toEmail: '%s'\n", toEmail ? toEmail : "NULL");
  
  // Vérifications préalables
  if (!subject) {
    Serial.println(F("[Mail] ❌ ERREUR: subject est NULL"));
    return false;
  }
  if (!toEmail) {
    Serial.println(F("[Mail] ❌ ERREUR: toEmail est NULL"));
    return false;
  }
  if (message.length() == 0) {
    Serial.println(F("[Mail] ❌ ERREUR: message vide"));
    return false;
  }
  
  String alertSubject = String("FFP3 - ") + subject;
  Serial.printf("[Mail] alertSubject créer: '%s'\n", alertSubject.c_str());
  
  // Ajouter le rapport temporel détaillé au message d'alerte
  String enhancedMessage = message;
  Serial.printf("[Mail] enhancedMessage initial: %d chars\n", enhancedMessage.length());
  
  // Créer une instance temporaire de Diagnostics pour accéder aux informations de redémarrage
  Diagnostics tempDiag;
  tempDiag.begin(); // Initialise avec les données NVS
  enhancedMessage += buildDetailedTimeReport(tempDiag);
  Serial.printf("[Mail] enhancedMessage final: %d chars\n", enhancedMessage.length());
  
  Serial.println(F("[Mail] ===== ENVOI D'ALERTE ====="));
  Serial.printf("[Mail] Type: Alerte système\n");
  Serial.printf("[Mail] Destinataire: %s\n", toEmail);
  Serial.printf("[Mail] Objet original: %s\n", subject);
  Serial.printf("[Mail] Objet final: %s\n", alertSubject.c_str());
  Serial.println(F("[Mail] ==========================="));
  
  bool result = send(alertSubject.c_str(), enhancedMessage.c_str(), "User", toEmail);
  Serial.printf("[Mail] ===== RÉSULTAT SENDALERT: %s =====\n", result ? "SUCCESS" : "FAILED");
  return result;
}

bool Mailer::sendSleepMail(const char* reason, uint32_t sleepDurationSeconds, const SensorReadings& readings) {
  String sleepSubject = String("FFP3 - Mise en veille");
  
  String sleepMessage;
  sleepMessage.reserve(1024);
  sleepMessage += "Le système FFP3CS entre en veille légère\n\n";
  sleepMessage += "-- INFORMATIONS DE MISE EN VEILLE --\n";
  sleepMessage += "Raison: "; sleepMessage += reason; sleepMessage += "\n";
  sleepMessage += "Durée prévue: "; sleepMessage += String(sleepDurationSeconds); sleepMessage += " secondes\n";
  sleepMessage += "Timestamp: "; 
  
  // Ajouter l'heure actuelle
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    sleepMessage += timeBuf;
  } else {
    sleepMessage += "Erreur heure";
  }
  sleepMessage += "\n";
  
  // Ajouter les informations système détaillées
  // UTILISE LES DERNIÈRES LECTURES (passées en paramètre) au lieu de relire les capteurs
  sleepMessage += "\n-- ÉTAT SYSTÈME AVANT VEILLE --\n";
  extern SystemActuators acts;
  sleepMessage += "- Temp eau: "; sleepMessage += String(readings.tempWater, 1); sleepMessage += " °C\n";
  sleepMessage += "- Temp air: "; sleepMessage += String(readings.tempAir, 1); sleepMessage += " °C\n";
  sleepMessage += "- Aqua lvl: "; sleepMessage += String(readings.wlAqua); sleepMessage += " cm\n";
  sleepMessage += "- Réserve lvl: "; sleepMessage += String(readings.wlTank); sleepMessage += " cm\n";
  sleepMessage += "- Pompe aquarium: "; sleepMessage += (acts.isAquaPumpRunning() ? "ON" : "OFF"); sleepMessage += "\n";
  sleepMessage += "- Pompe réservoir: "; sleepMessage += (acts.isTankPumpRunning() ? "ON" : "OFF"); sleepMessage += "\n";
  sleepMessage += "- Chauffage: "; sleepMessage += (acts.isHeaterOn() ? "ON" : "OFF"); sleepMessage += "\n";
  sleepMessage += "- Lumière: "; sleepMessage += (acts.isLightOn() ? "ON" : "OFF"); sleepMessage += "\n";
  
  // Ajouter les informations de délai d'activité
  sleepMessage += "\n-- Configuration Sleep --\n";
  sleepMessage += "- Délai d'activité: Configuration adaptative\n";
  sleepMessage += "- Mode adaptatif: ACTIF\n";
  
  Serial.println(F("[Mail] ===== ENVOI MAIL VEILLE ====="));
  Serial.printf("[Mail] Raison: %s\n", reason);
  Serial.printf("[Mail] Durée: %u s\n", sleepDurationSeconds);
  Serial.println(F("[Mail] ⚡ Utilisation des dernières lectures (pas de nouvelle lecture capteurs)"));
  Serial.println(F("[Mail] =============================="));
  
  return send(sleepSubject.c_str(), sleepMessage.c_str(), "User", Config::DEFAULT_MAIL_TO);
}

bool Mailer::sendWakeMail(const char* reason, uint32_t actualSleepSeconds, const SensorReadings& readings) {
  String wakeSubject = String("FFP3 - Réveil du système");
  
  String wakeMessage;
  wakeMessage.reserve(1024);
  wakeMessage += "Le système FFP3CS s'est réveillé de sa veille légère\n\n";
  wakeMessage += "-- INFORMATIONS DE RÉVEIL --\n";
  wakeMessage += "Raison: "; wakeMessage += reason; wakeMessage += "\n";
  wakeMessage += "Durée réelle de veille: "; wakeMessage += String(actualSleepSeconds); wakeMessage += " secondes\n";
  wakeMessage += "Timestamp: "; 
  
  // Ajouter l'heure actuelle
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    wakeMessage += timeBuf;
  } else {
    wakeMessage += "Erreur heure";
  }
  wakeMessage += "\n";
  
  // Ajouter les informations système détaillées
  // UTILISE LES DERNIÈRES LECTURES (passées en paramètre) au lieu de relire les capteurs
  wakeMessage += "\n-- ÉTAT SYSTÈME AU RÉVEIL --\n";
  extern SystemActuators acts;
  wakeMessage += "- Temp eau: "; wakeMessage += String(readings.tempWater, 1); wakeMessage += " °C\n";
  wakeMessage += "- Temp air: "; wakeMessage += String(readings.tempAir, 1); wakeMessage += " °C\n";
  wakeMessage += "- Aqua lvl: "; wakeMessage += String(readings.wlAqua); wakeMessage += " cm\n";
  wakeMessage += "- Réserve lvl: "; wakeMessage += String(readings.wlTank); wakeMessage += " cm\n";
  wakeMessage += "- Pompe aquarium: "; wakeMessage += (acts.isAquaPumpRunning() ? "ON" : "OFF"); wakeMessage += "\n";
  wakeMessage += "- Pompe réservoir: "; wakeMessage += (acts.isTankPumpRunning() ? "ON" : "OFF"); wakeMessage += "\n";
  wakeMessage += "- Chauffage: "; wakeMessage += (acts.isHeaterOn() ? "ON" : "OFF"); wakeMessage += "\n";
  wakeMessage += "- Lumière: "; wakeMessage += (acts.isLightOn() ? "ON" : "OFF"); wakeMessage += "\n";
  
  // Ajouter les informations de délai d'activité
  wakeMessage += "\n-- Configuration Sleep --\n";
  wakeMessage += "- Délai d'activité: Configuration adaptative\n";
  wakeMessage += "- Mode adaptatif: ACTIF\n";
  
  // Ajouter les informations de connexion WiFi
  wakeMessage += "\n-- CONNEXION RÉSEAU --\n";
  if (WiFi.status() == WL_CONNECTED) {
    wakeMessage += "- WiFi: Connecté\n";
    wakeMessage += "- SSID: "; wakeMessage += WiFi.SSID(); wakeMessage += "\n";
    wakeMessage += "- IP: "; wakeMessage += WiFi.localIP().toString(); wakeMessage += "\n";
    wakeMessage += "- RSSI: "; wakeMessage += String(WiFi.RSSI()); wakeMessage += " dBm\n";
  } else {
    wakeMessage += "- WiFi: Déconnecté\n";
  }
  
  Serial.println(F("[Mail] ===== ENVOI MAIL RÉVEIL ====="));
  Serial.printf("[Mail] Raison: %s\n", reason);
  Serial.printf("[Mail] Durée veille: %u s\n", actualSleepSeconds);
  Serial.println(F("[Mail] =============================="));
  
  return send(wakeSubject.c_str(), wakeMessage.c_str(), "User", Config::DEFAULT_MAIL_TO);
}
#endif
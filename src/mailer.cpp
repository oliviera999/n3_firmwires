// Mailer implementation can be compiled out to save flash on ESP32 WROOM
#include "mailer.h"
#include "project_config.h"
#include "system_sensors.h"
#include "system_actuators.h"
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

  // Horodatage (epoch si pas de NTP)
  time_t now = time(nullptr);
  footer += "- Epoch: "; footer += String((unsigned long)now); footer += "\n";
  if (now > 100000) {
    struct tm tmInfo;
    localtime_r(&now, &tmInfo);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmInfo);
    footer += "- Local time: "; footer += tbuf; footer += "\n";
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
  Preferences prefs;
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
  footer += "- Tension: "; footer += String(rs.voltageMv); footer += " mV\n";
  footer += "- Diff marée: "; footer += String(diffM); footer += "\n";
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

bool Mailer::begin() {
  // Prépare uniquement la configuration SMTP.
  // La connexion TLS/SMTP sera établie à la première utilisation dans send().

  _cfg = Session_Config();
  _cfg.server.host_name = Config::SMTP_HOST;
  _cfg.server.port      = Config::SMTP_PORT_SSL;
  _cfg.login.email      = Secrets::AUTHOR_EMAIL;
  _cfg.login.password   = Secrets::AUTHOR_PASSWORD;

  // Pas de connexion ici pour éviter le blocage au démarrage
  _ready = false;
  Serial.println("[Mail] SMTP prêt (connexion différée)");
  return true;
}

bool Mailer::send(const char* subject, const char* message, const char* toName, const char* toEmail) {
  if (!_ready || !_smtp.connected()) {
    // tentative de reconnexion
    _smtp.closeSession();
    _ready = _smtp.connect(&_cfg);
    if(!_ready){
      Serial.println(F("[Mail] Reconnexion SMTP échouée"));
      return false;
    }
  }
  // Construire l'objet avec l'environnement de manière explicite
  const char* envName = CompatibilityUtils::getEnvironmentName();
  String subjectWithEnv;
  subjectWithEnv.reserve((envName ? strlen(envName) : 0) + (subject ? strlen(subject) : 0) + 4);
  subjectWithEnv += "[";
  subjectWithEnv += (envName ? envName : "");
  subjectWithEnv += "] ";
  subjectWithEnv += (subject ? subject : "");
  SMTP_Message msg;
  String fromName;
  fromName.reserve((envName ? strlen(envName) : 0) + 8);
  fromName += "FFP3 [";
  fromName += (envName ? envName : "");
  fromName += "]";
  msg.sender.name  = fromName.c_str();
  msg.sender.email = Secrets::AUTHOR_EMAIL;
  msg.subject      = subjectWithEnv.c_str();
  msg.addRecipient(toName, toEmail);
  String finalMessage = String(message);
  finalMessage += buildSystemInfoFooter();
  msg.text.content = finalMessage;
  
  // Affichage des détails du mail avant envoi
  Serial.println(F("[Mail] ===== DÉTAILS DU MAIL ====="));
  Serial.printf("[Mail] De: %s <%s>\n", msg.sender.name, msg.sender.email);
  Serial.printf("[Mail] À: %s <%s>\n", toName, toEmail);
  Serial.printf("[Mail] Objet: %s\n", subjectWithEnv.c_str());
  Serial.println(F("[Mail] Contenu:"));
  Serial.println(finalMessage);
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
#endif

#if FEATURE_MAIL && FEATURE_MAIL != 0
bool Mailer::sendAlert(const char* subject, const String& message, const char* toEmail) {
  String alertSubject = String("FFP3 - ") + subject;
  Serial.println(F("[Mail] ===== ENVOI D'ALERTE ====="));
  Serial.printf("[Mail] Type: Alerte système\n");
  Serial.printf("[Mail] Destinataire: %s\n", toEmail);
  Serial.printf("[Mail] Objet original: %s\n", subject);
  Serial.printf("[Mail] Objet final: %s\n", alertSubject.c_str());
  Serial.println(F("[Mail] ==========================="));
  return send(alertSubject.c_str(), message.c_str(), "User", toEmail);
} 
#endif
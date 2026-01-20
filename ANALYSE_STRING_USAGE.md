# Analyse de l'utilisation de String dans le firmware FFP5CS

**Date**: 2026-01-17  
**Objectif**: Identifier toutes les utilisations de `String` Arduino et évaluer les risques de fragmentation mémoire

---

## 📊 Résumé exécutif

**Total d'occurrences**: ~495 lignes avec `String`  
**Fichiers concernés**: 5 fichiers principaux + plusieurs fichiers secondaires

### Risques identifiés

| Fichier | Risque | Occurrences | Priorité |
|---------|--------|-------------|----------|
| `diagnostics.cpp` | 🔴 ÉLEVÉ | ~80 | CRITIQUE |
| `mailer.cpp` | 🔴 ÉLEVÉ | ~120 | CRITIQUE |
| `web_client.cpp` | 🟡 MOYEN | ~100 | MOYENNE |
| `web_server.cpp` | 🟡 MOYEN | ~150 | MOYENNE |
| `data_queue.cpp` | 🟢 FAIBLE | ~20 | BASSE |

---

## 🔴 FICHIERS CRITIQUES

### 1. `diagnostics.cpp` - RISQUE ÉLEVÉ

#### Problèmes identifiés

**1.1. `generateRestartReport()` - Ligne 357**
```cpp
String Diagnostics::generateRestartReport() const {
  String report;
  report.reserve(2048); // Bon début mais...
  
  // ❌ PROBLÈME: Multiples concaténations String
  report += "Raison du redémarrage: ";
  report += resetReasonToString(resetReason);
  report += " (code ";
  report += String((int)resetReason);  // ❌ Allocation temporaire
  report += ")";
  // ... répété ~30 fois dans la fonction
}
```

**Impact**: 
- Chaque `+=` peut causer une réallocation mémoire
- `String((int)resetReason)` crée des objets temporaires
- Fonction appelée au boot (risque de fragmentation précoce)

**Recommandation**: Utiliser `snprintf()` avec un buffer statique de 2048 bytes

---

**1.2. `getRebootReason()` - Ligne 151**
```cpp
String Diagnostics::getRebootReason() const {
  esp_reset_reason_t r = esp_reset_reason();
  return resetReasonToString(r);  // ❌ Retourne String temporaire
}
```

**Impact**: Allocation à chaque appel

**Recommandation**: Retourner `const char*` directement

---

**1.3. `recordOtaResult()` - Ligne 353**
```cpp
_stats.lastOtaError = errorMsg ? String(errorMsg) : String("");  // ❌ Allocation
```

**Impact**: Allocation même pour chaîne vide

**Recommandation**: Utiliser `char[]` statique dans la structure

---

**1.4. `capturePanicInfo()` - Lignes 576, 587, 594-595**
```cpp
_stats.panicInfo.additionalInfo += " (Reset code: " + String((int)rtcReason) + ")";  // ❌
_stats.panicInfo.additionalInfo += "Core 1 reason differs: " + String((int)rtcReason1);  // ❌
_stats.panicInfo.additionalInfo += "ESP reset reason: " + String((int)resetReason);  // ❌
```

**Impact**: Multiples allocations temporaires lors d'un panic

**Recommandation**: Utiliser `snprintf()` pour construire la chaîne complète

---

**1.5. `update()` - Ligne 178**
```cpp
_stats.taskStats = String(buf);  // ❌ Allocation à chaque update
```

**Impact**: Allocation toutes les ~100ms si `UPDATE_INTERVAL_MS` est court

**Recommandation**: Utiliser `char[]` statique dans `DiagnosticStats`

---

### 2. `mailer.cpp` - RISQUE ÉLEVÉ

#### Problèmes identifiés

**2.1. `sendAlertSync()` - Lignes 1109-1144**
```cpp
String alertSubject = String("FFP5CS - ") + subject;  // ❌ Allocation
String enhancedMessage = message;  // ❌ Copie (peut être grande)
enhancedMessage += timeReport;  // ❌ Concaténation
enhancedMessage += buildSystemInfoFooter();  // ❌ Concaténation
enhancedMessage += "\n[Footer complet déjà inclus]";  // ❌
```

**Impact**: 
- Plusieurs allocations pour un seul email
- `enhancedMessage` peut devenir très grande (rapport complet)
- Risque de fragmentation lors de l'envoi d'alertes critiques

**Recommandation**: Utiliser un buffer statique de taille fixe (ex: 4096 bytes)

---

**2.2. `sendSleepMail()` - Lignes 1162-1208**
```cpp
String sleepMessage;
sleepMessage.reserve(1024);  // ✅ Bon début
sleepMessage += "Le système FFP5CS entre en veille légère\n\n";
sleepMessage += "-- INFORMATIONS DE MISE EN VEILLE --\n";
sleepMessage += "Raison: "; sleepMessage += reason; sleepMessage += "\n";  // ❌ Multiples +=
sleepMessage += "Durée prévue: "; sleepMessage += String(sleepDurationSeconds); sleepMessage += " secondes\n";  // ❌
// ... ~15 concaténations supplémentaires
```

**Impact**: Multiples réallocations potentielles

**Recommandation**: Utiliser `snprintf()` avec un buffer statique

---

**2.3. `sendWakeMail()` - Lignes 1211-1268**
```cpp
String wakeMessage;
wakeMessage.reserve(1024);  // ✅ Bon début
// ... même problème que sendSleepMail()
wakeMessage += "- RSSI: "; wakeMessage += String(WiFi.RSSI()); wakeMessage += " dBm\n";  // ❌
```

**Impact**: Même problème que `sendSleepMail()`

**Recommandation**: Utiliser `snprintf()` avec un buffer statique

---

**2.4. `sendSync()` - Ligne 986**
```cpp
static String finalMessageStatic;  // ⚠️ String statique mais toujours String
finalMessageStatic = message ? message : "";
finalMessageStatic += buildLightFooter();  // ❌ Concaténation
```

**Impact**: 
- `finalMessageStatic` est statique mais reste un `String` (allocation dynamique)
- Concaténation peut causer réallocation

**Recommandation**: Utiliser `char[]` statique de taille fixe

---

**2.5. `buildDetailedTimeReport()` - Ligne 825**
```cpp
String restartReport = diagnostics.generateRestartReport();  // ❌ Allocation
size_t restartLen = restartReport.length();
if (restartLen > 0 && restartLen < remaining) {
  strncpy(buf, restartReport.c_str(), remaining - 1);  // ⚠️ Copie depuis String
}
```

**Impact**: Allocation temporaire pour le rapport de redémarrage

**Recommandation**: Modifier `generateRestartReport()` pour écrire directement dans un buffer

---

**2.6. `sendAlert()` - Ligne 1404**
```cpp
size_t msgLen = message.length();
if (msgLen >= sizeof(item.message)) {
  msgLen = sizeof(item.message) - 1;
}
strncpy(item.message, message.c_str(), msgLen);  // ⚠️ Copie depuis String
```

**Impact**: `message` est un `String&` passé en paramètre (allocation externe)

**Recommandation**: Accepter `const char*` au lieu de `const String&`

---

## 🟡 FICHIERS MOYENS

### 3. `web_client.cpp` - RISQUE MOYEN

#### Problèmes identifiés

**3.1. `httpRequest()` - Lignes 55, 71, 176**
```cpp
bool WebClient::httpRequest(const String& url, const String& payload, String& response) {
  // ❌ Paramètres String (copies potentielles)
  size_t payloadLen = payload.length();
  bool isSecure = url.startsWith("https://");  // ⚠️ Méthode String
  response = _http.getString();  // ❌ Allocation dans response
}
```

**Impact**: 
- Copies de `url` et `payload` si passés par valeur
- `response` alloué dynamiquement

**Recommandation**: 
- Utiliser `const char*` pour `url` et `payload`
- Utiliser buffer statique pour `response` ou `char[]` de taille fixe

---

**3.2. `sendMeasurements()` - Lignes 364-376**
```cpp
auto fmtFloat = [](float v) -> String {  // ❌ Retourne String temporaire
  if (isnan(v)) return String("");
  char buf[16];
  int n = snprintf(buf, sizeof(buf), "%.1f", v);
  return String(buf, n);  // ❌ Allocation
};

auto fmtUltrasonic = [](uint16_t v) -> String {  // ❌ Même problème
  if (v == 0) return String("");
  char buf[8];
  int n = snprintf(buf, sizeof(buf), "%u", (unsigned)v);
  return String(buf, n);  // ❌ Allocation
};
```

**Impact**: Allocations temporaires multiples dans une boucle

**Recommandation**: Modifier pour écrire directement dans le buffer de payload

---

**3.3. `fetchRemoteState()` - Lignes 597, 604**
```cpp
String payload = String(payloadBuffer);  // ❌ Allocation depuis buffer
// ...
Serial.printf("[GET] %s ... (truncated)\n", payload.substring(0,600).c_str());  // ❌ substring() crée String temporaire
```

**Impact**: 
- Allocation inutile (on a déjà `payloadBuffer`)
- `substring()` crée un String temporaire

**Recommandation**: Utiliser directement `payloadBuffer` avec `strncpy()` pour le preview

---

**3.4. `sendHeartbeat()` - Lignes 663, 689**
```cpp
String payload;  // ❌ Non utilisé finalement (code mort?)
String resp;  // ❌ Allocation pour réponse
```

**Impact**: Variables non utilisées ou allocations inutiles

**Recommandation**: Supprimer ou utiliser buffers statiques

---

**3.5. `postRaw()` - Lignes 706, 727, 735, 761**
```cpp
bool WebClient::postRaw(const String& payload) {  // ❌ Paramètre String
  size_t estimatedSize = payloadLen + _apiKey.length() + strlen(ProjectConfig::BOARD_TYPE) + 32;
  // ...
  bool hasApi = (payload.indexOf("api_key=") == 0);  // ⚠️ Méthode String
  // ...
  String respPrimary;  // ❌ Allocation
}
```

**Impact**: 
- Copie potentielle de `payload`
- Méthodes String (`indexOf()`, `length()`)
- Allocation pour `respPrimary`

**Recommandation**: Utiliser `const char*` et buffers statiques

---

### 4. `web_server.cpp` - RISQUE MOYEN

#### Problèmes identifiés

**4.1. Paramètres HTTP - Lignes 195, 363, 671, 1113, etc.**
```cpp
String c = req->getParam("cmd")->value();  // ❌ Allocation à chaque requête
String rel = req->getParam("relay")->value();  // ❌
auto getParam = [req](const char* name)->String {  // ❌ Retourne String
  return req->hasParam(name, true) ? req->getParam(name, true)->value() : String();
};
```

**Impact**: 
- Allocations à chaque requête HTTP
- Lambda retourne String (allocation)

**Recommandation**: 
- Utiliser `const char*` directement depuis `req->getParam()->value().c_str()`
- Attention: pointer peut être invalide après la requête, donc copier dans buffer statique si nécessaire

---

**4.2. JSON serialization - Lignes 730, 1431**
```cpp
String saveStr; serializeJson(nvsDoc, saveStr);  // ❌ Allocation
String json; json.reserve(512); serializeJson(doc, json);  // ⚠️ Allocation contrôlée
```

**Impact**: Allocation pour JSON (peut être grande)

**Recommandation**: Utiliser `JsonDocument` avec capacité fixe et `serializeJson()` vers buffer statique

---

**4.3. WiFi management - Lignes 1296, 1389, 1550**
```cpp
String data = String(buffer);  // ❌ Allocation depuis buffer
String ssid = data.substring(0, separator);  // ❌ Allocation temporaire
String password = data.substring(separator + 1);  // ❌ Allocation temporaire
```

**Impact**: Multiples allocations pour parser SSID/password

**Recommandation**: Utiliser `strtok()` ou parsing manuel avec buffers statiques

---

**4.4. NVS operations - Lignes 1154, 638**
```cpp
String js = value;  // ❌ Copie String
String remoteJson;  // ❌ Allocation
g_nvsManager.loadJsonDecompressed(NVS_NAMESPACES::CONFIG, "remote_json", remoteJson, "");
```

**Impact**: Allocation pour JSON décompressé (peut être grande)

**Recommandation**: Utiliser buffer statique de taille fixe

---

## 🟢 FICHIERS FAIBLES

### 5. `data_queue.cpp` - RISQUE FAIBLE

#### Optimisations déjà en place ✅

**5.1. `pop()` - Lignes 111-134**
```cpp
// ✅ DÉJÀ OPTIMISÉ: Utilise buffer fixe
const size_t LINE_BUF_SIZE = 512;
char lineBuf[LINE_BUF_SIZE];
size_t len = src.readBytesUntil('\n', lineBuf, LINE_BUF_SIZE - 1);
```

**Impact**: Pas d'allocation dynamique ✅

---

**5.2. Problèmes restants**

**Ligne 46, 89, 156, 200, 238**
```cpp
bool DataQueue::push(const String& payload) {  // ⚠️ Paramètre String
String DataQueue::pop() {  // ❌ Retourne String
String DataQueue::peek() {  // ❌ Retourne String
  String first = file.readStringUntil('\n');  // ❌ Allocation
  String line = file.readStringUntil('\n');  // ❌ Allocation
```

**Impact**: 
- `push()` accepte `String&` (OK si appelé avec String existant)
- `pop()` et `peek()` retournent String (allocation)
- `readStringUntil()` alloue dynamiquement

**Recommandation**: 
- Modifier `pop()` et `peek()` pour utiliser buffer statique
- Utiliser `readBytesUntil()` au lieu de `readStringUntil()`

---

## 📋 AUTRES FICHIERS

### 6. `automatism.cpp` - Lignes 402, 408, 410
```cpp
String json;  // ❌ Allocation pour JSON
String emailFromNVS;  // ❌ Allocation
strncpy(_emailAddress, emailFromNVS.c_str(), EmailConfig::MAX_EMAIL_LENGTH - 1);
```

**Impact**: Allocations pour JSON et email

**Recommandation**: Utiliser buffers statiques

---

## 🎯 PLAN D'ACTION RECOMMANDÉ

### Priorité 1 - CRITIQUE (Impact fragmentation mémoire élevé)

1. **`diagnostics.cpp::generateRestartReport()`**
   - Remplacer par `snprintf()` avec buffer statique de 2048 bytes
   - Éliminer toutes les concaténations `+=`

2. **`mailer.cpp::sendAlertSync()`**
   - Utiliser buffer statique de 4096 bytes pour `enhancedMessage`
   - Éliminer les concaténations `+=`

3. **`mailer.cpp::sendSleepMail()` et `sendWakeMail()`**
   - Utiliser `snprintf()` avec buffer statique de 1024 bytes

4. **`diagnostics.cpp::update()`**
   - Remplacer `_stats.taskStats` (String) par `char[]` statique

### Priorité 2 - MOYENNE (Impact fragmentation mémoire modéré)

5. **`web_client.cpp::httpRequest()`**
   - Changer signature pour `const char*` au lieu de `String&`
   - Utiliser buffer statique pour `response`

6. **`web_client.cpp::sendMeasurements()`**
   - Modifier lambdas `fmtFloat` et `fmtUltrasonic` pour écrire directement dans buffer

7. **`web_server.cpp` - Paramètres HTTP**
   - Utiliser `const char*` directement depuis `req->getParam()->value().c_str()`
   - Copier dans buffer statique si nécessaire pour persistance

### Priorité 3 - BASSE (Impact fragmentation mémoire faible)

8. **`data_queue.cpp::pop()` et `peek()`**
   - Modifier pour utiliser buffer statique au lieu de retourner String

9. **`automatism.cpp`**
   - Utiliser buffers statiques pour JSON et email

---

## 📊 ESTIMATION GAIN MÉMOIRE

### Fragmentation évitée (estimations)

| Fichier | Allocations évitées | Gain estimé |
|---------|---------------------|-------------|
| `diagnostics.cpp` | ~50-100 allocations/boot | ~10-20 KB |
| `mailer.cpp` | ~20-30 allocations/email | ~5-10 KB |
| `web_client.cpp` | ~10-20 allocations/requête | ~2-5 KB |
| `web_server.cpp` | ~5-10 allocations/requête | ~1-3 KB |
| **TOTAL** | **~85-160 allocations** | **~18-38 KB** |

### Impact sur stabilité

- ✅ Réduction fragmentation mémoire
- ✅ Heap libre plus stable
- ✅ Moins de risques de crash par épuisement mémoire
- ✅ Meilleure performance (moins d'allocations/libérations)

---

## ⚠️ NOTES IMPORTANTES

1. **Compatibilité API**: Certaines modifications nécessiteront de changer les signatures de fonctions
   - Vérifier tous les appels avant modification
   - Tester sur hardware réel après chaque changement

2. **Taille buffers**: Définir des tailles maximales raisonnables
   - Rapports: 2048-4096 bytes
   - Emails: 4096 bytes
   - JSON: 1024-2048 bytes
   - Paramètres HTTP: 256 bytes

3. **Tests**: Tester particulièrement:
   - Boot avec panic info
   - Envoi emails (alertes critiques)
   - Requêtes HTTP multiples
   - Requêtes Web simultanées

---

## 🔍 MÉTHODOLOGIE DE DÉTECTION

Pour trouver toutes les utilisations:
```bash
grep -r "\bString\b" src/ | wc -l  # ~495 occurrences
grep -r "\.c_str()" src/ | wc -l   # ~363 occurrences
grep -r "\.length()" src/ | wc -l   # ~100+ occurrences
```

---

**Conclusion**: Le firmware utilise massivement `String` Arduino, ce qui cause un risque significatif de fragmentation mémoire. Les fichiers critiques (`diagnostics.cpp` et `mailer.cpp`) doivent être optimisés en priorité pour améliorer la stabilité du système.

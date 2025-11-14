# 📊 Analyse de la Compilation - Firmware wroom-prod

## 🎯 Vue d'ensemble

Ce document explique **ce qui est compilé ou pas** dans le firmware `wroom-prod` pour l'ESP32-WROOM-32 (4MB Flash).

## 📋 Configuration de la partition

**Fichier**: `partitions_esp32_wroom_ota_fs_medium.csv`

```
nvs,data,nvs,0x9000,0x5000          # 20 KB - NVS (paramètres)
otadata,data,ota,0xE000,0x2000       # 8 KB - OTA data
app0,app,ota_0,0x10000,0x1A0000     # 1.625 MB - Application slot 0
app1,app,ota_1,,0x1A0000            # 1.625 MB - Application slot 1
littlefs,data,littlefs,,0x0B0000    # 704 KB - Système de fichiers
```

**Taille maximale du firmware**: **1.625 MB (1,703,936 bytes)**

---

## ✅ Ce qui EST compilé dans wroom-prod

### 🔧 Flags de compilation activés

```ini
-DBOARD_WROOM                    # Cible ESP32-WROOM
-DPROFILE_PROD                   # Mode production
-DNDEBUG                         # Désactive les assertions
-Os                              # Optimisation taille (vs -O1 de base)
-DDISABLE_ASYNC_WEBSERVER        # ⚠️ SERVEUR WEB DÉSACTIVÉ
-DENABLE_SERIAL_MONITOR=0        # Moniteur série désactivé
-DENABLE_SENSOR_LOGS=0           # Logs capteurs désactivés
-DCORE_DEBUG_LEVEL=0             # Debug IDF minimal
-DLOG_LEVEL=0                    # Logs système minimaux
```

### 📚 Bibliothèques compilées (TOUTES)

**Toutes les bibliothèques de `lib_deps` sont compilées**, même si certaines fonctionnalités sont désactivées :

1. ✅ **ESPAsyncWebServer@3.5.0** - Compilée mais **non utilisée** (DISABLE_ASYNC_WEBSERVER)
2. ✅ **Adafruit Unified Sensor@1.1.14** - Utilisée
3. ✅ **DHT sensor library@1.4.6** - Utilisée
4. ✅ **Adafruit GFX Library@1.11.10** - Utilisée (OLED)
5. ✅ **Adafruit SSD1306@2.5.9** - Utilisée (OLED)
6. ✅ **Adafruit BusIO@1.17.4** - Utilisée (I2C/SPI)
7. ✅ **OneWire@2.3.8** - Utilisée (DS18B20)
8. ✅ **DallasTemperature@3.11.0** - Utilisée (DS18B20)
9. ✅ **ESP32Servo@3.0.5** - Utilisée
10. ✅ **ESP32Time@2.0.0** - Utilisée
11. ✅ **ArduinoJson@7.4.2** - Utilisée
12. ✅ **ESP Mail Client@3.4.24** - Utilisée
13. ✅ **WebSockets@2.7.0** - Utilisée (WebSocket temps réel)

### 🎯 Fonctionnalités activées (FEATURE_*)

```ini
-DFEATURE_MAIL=1                 # ✅ Email activé
-DFEATURE_ARDUINO_OTA=1          # ✅ Arduino OTA activé
-DFEATURE_WEBSOCKET=1            # ✅ WebSocket activé
-DFEATURE_REALTIME_WEBSOCKET=1   # ✅ WebSocket temps réel activé
-DFEATURE_ASSET_BUNDLER=1        # ✅ Bundler d'assets activé
-DFEATURE_JSON_POOL=1            # ✅ Pool JSON activé
-DFEATURE_NETWORK_OPTIMIZER=1    # ✅ Optimiseur réseau activé
-DFEATURE_PUMP_STATS_CACHE=1     # ✅ Cache stats pompe activé
-DFEATURE_SENSOR_CACHE=1         # ✅ Cache capteurs activé
```

### 📁 Fichiers source compilés (TOUS)

**Tous les fichiers `.cpp` dans `src/` sont compilés**, mais certains contiennent du code conditionnel :

1. ✅ `app.cpp` - Point d'entrée principal
2. ✅ `wifi_manager.cpp` - Gestion WiFi
3. ✅ `sensors.cpp` / `system_sensors.cpp` - Capteurs
4. ✅ `actuators.cpp` / `system_actuators.cpp` - Actionneurs
5. ✅ `display_view.cpp` - Affichage OLED
6. ✅ `mailer.cpp` - Envoi d'emails
7. ✅ `web_client.cpp` - Client HTTP (serveur distant)
8. ✅ `web_server.cpp` - **Serveur web local (code minimal si DISABLE_ASYNC_WEBSERVER)**
9. ✅ `ota_manager.cpp` - Gestion OTA
10. ✅ `config.cpp` - Configuration
11. ✅ `automatism.cpp` + sous-modules - Logique métier
12. ✅ `realtime_websocket.cpp` - WebSocket temps réel
13. ✅ `json_pool.cpp` - Pool JSON
14. ✅ `sensor_cache.cpp` - Cache capteurs
15. ✅ `pump_stats_cache.cpp` - Cache stats pompe
16. ✅ `network_optimizer.cpp` - Optimiseur réseau
17. ✅ `asset_bundler.cpp` - Bundler d'assets
18. ✅ Et tous les autres fichiers...

### 🔍 Code conditionnel compilé

#### Dans `web_server.cpp`:
```cpp
#ifdef DISABLE_ASYNC_WEBSERVER
  // Mode minimal - seulement quelques lignes
  Serial.println("[WebServer] Mode minimal - serveur web désactivé");
  return true;
#else
  // Code complet du serveur web (NON COMPILÉ)
  _server = new AsyncWebServer(80);
  // ... routes, handlers, etc.
#endif
```

#### Dans `app.cpp`:
```cpp
#if FEATURE_OTA && FEATURE_OTA != 0 && FEATURE_ARDUINO_OTA && FEATURE_ARDUINO_OTA != 0
  #include <ArduinoOTA.h>  // ✅ COMPILÉ (FEATURE_ARDUINO_OTA=1)
#endif
```

#### Dans `mailer.cpp`:
```cpp
#if FEATURE_MAIL && FEATURE_MAIL != 0
  // Code complet du mailer (✅ COMPILÉ)
#else
  bool Mailer::begin() { return true; }  // Stub minimal
#endif
```

#### Dans `display_view.cpp`:
```cpp
#if FEATURE_OLED
  // Code complet OLED (✅ COMPILÉ)
#else
  // Code minimal
#endif
```

---

## ❌ Ce qui N'EST PAS compilé dans wroom-prod

### 🚫 Code exclu par flags

1. **Serveur web local (AsyncWebServer)**
   - Flag: `-DDISABLE_ASYNC_WEBSERVER`
   - Impact: Le code dans `web_server.cpp` entre `#ifndef DISABLE_ASYNC_WEBSERVER` n'est **PAS compilé**
   - **MAIS**: La bibliothèque `ESPAsyncWebServer` est **toujours liée** (linkée) même si non utilisée

2. **Moniteur série**
   - Flag: `-DENABLE_SERIAL_MONITOR=0`
   - Impact: Les appels `Serial.*` sont compilés mais peuvent être optimisés par le linker

3. **Logs de capteurs**
   - Flag: `-DENABLE_SENSOR_LOGS=0`
   - Impact: Les logs de capteurs sont exclus

4. **Debug IDF/ESP32**
   - Flags: `-DCORE_DEBUG_LEVEL=0`, `-DLOG_LEVEL=0`
   - Impact: Les logs système ESP-IDF sont minimaux

5. **Arduino OTA (selon project_config.h)**
   - Dans `project_config.h`: `FEATURE_ARDUINO_OTA = 0` en PROD
   - **MAIS** dans `platformio.ini`: `-DFEATURE_ARDUINO_OTA=1` (contradiction!)
   - **Résultat**: Arduino OTA est **activé** (le flag platformio.ini prend le dessus)

### 📦 Bibliothèques non utilisées mais compilées

**Problème identifié**: Même si `DISABLE_ASYNC_WEBSERVER` est défini, la bibliothèque `ESPAsyncWebServer` est **toujours compilée et liée** car elle est dans `lib_deps` commun.

**Impact**: La bibliothèque prend de l'espace même si non utilisée.

---

## 🔍 Analyse détaillée des flags

### Flags d'optimisation

```ini
-Os                              # Optimisation taille (vs -O1 de base)
-ffunction-sections              # Sections par fonction
-fdata-sections                  # Sections par données
-Wl,--gc-sections                # Élimination code mort
-fno-exceptions                  # Pas d'exceptions C++
-fno-rtti                        # Pas de RTTI
-fno-threadsafe-statics          # Pas de statiques thread-safe
-fno-use-cxa-atexit              # Pas de destructeurs globaux
-fno-builtin-printf              # Pas de printf builtin
-fno-builtin-sprintf             # Pas de sprintf builtin
-fno-builtin-vprintf             # Pas de vprintf builtin
-fno-builtin-vsprintf            # Pas de vsprintf builtin
-flto                            # Link Time Optimization
-ffat-lto-objects                # LTO avec objets intermédiaires
```

### Flags de fonctionnalités

```ini
-DSILENT_MODE                    # Mode silencieux
-DASYNCWEBSERVER_REGEX=0         # Regex désactivées dans AsyncWebServer
-DCOMPILER_MEMORY_LIMIT=512      # Limite mémoire compilateur
```

---

## ⚠️ Problèmes identifiés

### 1. **Contradiction FEATURE_ARDUINO_OTA**

- `platformio.ini` (ligne 101): `-DFEATURE_ARDUINO_OTA=1`
- `project_config.h` (ligne 776): `#define FEATURE_ARDUINO_OTA 0` (si PROFILE_PROD)

**Résultat**: Le flag `platformio.ini` prend le dessus → Arduino OTA est **activé** en prod.

### 2. **ESPAsyncWebServer toujours compilée**

Même avec `-DDISABLE_ASYNC_WEBSERVER`, la bibliothèque est dans `lib_deps` commun, donc **toujours compilée et liée**.

**Solution possible**: Utiliser `lib_ignore` conditionnel ou `lib_deps` par environnement.

### 3. **Toutes les bibliothèques compilées**

Toutes les 13 bibliothèques sont compilées même si certaines ne sont pas utilisées (ex: ESPAsyncWebServer si serveur désactivé).

---

## 📊 Estimation de la taille

### Composants compilés

| Composant | Taille estimée | Notes |
|-----------|----------------|-------|
| **Framework Arduino ESP32** | ~400-500 KB | Core système |
| **Bibliothèques** | ~600-800 KB | Toutes les 13 bibliothèques |
| **Code source** | ~300-400 KB | Tous les fichiers .cpp |
| **Optimisations LTO** | -10-15% | Réduction par LTO |
| **Code mort éliminé** | -5-10% | Par --gc-sections |
| **TOTAL ESTIMÉ** | **~1.2-1.5 MB** | Avant optimisations |

### Limite de partition

- **Taille max app0**: 1.625 MB (1,703,936 bytes)
- **Marge disponible**: ~200-500 KB (selon optimisations)

---

## 🎯 Recommandations pour réduire la taille

### 1. **Désactiver ESPAsyncWebServer si non utilisé**

```ini
[env:wroom-prod]
lib_ignore = 
    ESPAsyncWebServer  # Si DISABLE_ASYNC_WEBSERVER est défini
```

**Gain estimé**: ~100-150 KB

### 2. **Corriger la contradiction FEATURE_ARDUINO_OTA**

Décider si Arduino OTA doit être activé ou non en prod, et harmoniser les flags.

**Gain estimé**: ~50-100 KB (si désactivé)

### 3. **Utiliser des bibliothèques plus légères**

- Remplacer certaines bibliothèques Adafruit par des versions minimales
- Utiliser des alternatives plus légères si disponibles

**Gain estimé**: Variable (50-200 KB selon bibliothèques)

### 4. **Optimiser les assets web**

Si des assets sont compilés dans le firmware, les compresser ou les déplacer vers LittleFS.

**Gain estimé**: Variable

### 5. **Compiler conditionnellement les bibliothèques**

Utiliser `lib_deps` par environnement au lieu de `lib_deps` commun.

**Gain estimé**: Variable selon bibliothèques non utilisées

---

## 📝 Résumé

### ✅ Compilé et utilisé
- Framework Arduino ESP32
- Toutes les bibliothèques (13)
- Tous les fichiers source
- Fonctionnalités: Mail, OTA, WebSocket, OLED, Capteurs, Actionneurs
- Optimisations: LTO, gc-sections, -Os

### ❌ Non compilé (code exclu)
- Code serveur web local (AsyncWebServer) - **MAIS bibliothèque toujours liée**
- Logs de debug/développement
- Moniteur série (optimisé)
- Logs de capteurs

### ⚠️ Problèmes
- ESPAsyncWebServer compilée mais non utilisée
- Contradiction FEATURE_ARDUINO_OTA
- Toutes les bibliothèques compilées même si non utilisées

### 🎯 Actions recommandées
1. Exclure ESPAsyncWebServer si DISABLE_ASYNC_WEBSERVER
2. Harmoniser les flags FEATURE_ARDUINO_OTA
3. Analyser l'utilisation réelle de chaque bibliothèque
4. Compiler conditionnellement les bibliothèques non utilisées

---

*Document créé le: 2024-12-19*  
*Version firmware: 11.98*  
*Environnement analysé: wroom-prod*





#include "gpio_parser.h"
#include "automatism.h"
#include "nvs_manager.h"  // v11.108
#include "app_tasks.h"   // netRequestOtaCheck (déclenchement OTA depuis serveur distant)
#include "app_context.h"
#include "system_boot.h"  // SystemBoot::confirmOtaValidation (v14.17)
#include "config.h"
#include "gpio_bool_parse.h"  // cœur pur (token/int -> bool) extrait de parseBoolFromDoc
#include "automatism/feeding_command_resolver.h"  // résolveur pur compteur monotone 108/109
#include "nvs_keys.h"  // v15.0: clés compteurs nourrissage (feedExecP/feedExecG/feedSeed)
#include <WiFi.h>
#include <cstring>
#include <cmath>   // v11.164: fabsf pour comparaison float
#include <cstdlib> // v11.183: atoi/atof pour valeurs serveur en string

// v11.179: Variables d'état de détection de front (module-level pour reset au boot)
static bool s_lastTankState = false;
// v11.189: Reset ESP32 sur front montant uniquement (évite reboot en boucle si sync envoie 110:1)
static bool s_lastResetState = false;
// v15.0: les compteurs nourrissage 108/109 sont désormais persistés en NVS
// (feedExecP/feedExecG + flag feedSeed), plus de variables d'état edge module-level.

// --- NVS helpers compteur nourrissage (namespace CONFIG) ---------------------
namespace {
uint32_t loadFeedExec(const char* key) {
    unsigned long v = 0;
    g_nvsManager.loadULong(NVS_NAMESPACES::CONFIG, key, v, 0);
    return static_cast<uint32_t>(v);
}
void saveFeedExec(const char* key, uint32_t value) {
    g_nvsManager.saveULong(NVS_NAMESPACES::CONFIG, key, static_cast<unsigned long>(value));
}
bool loadFeedSeeded() {
    bool v = false;
    g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, NVSKeys::Config::FEED_SEEDED, v, false);
    return v;
}
void saveFeedSeeded(bool value) {
    g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, NVSKeys::Config::FEED_SEEDED, value);
}
}  // namespace

static bool tryStartOtaBeforeResetFromRemote() {
    extern AppContext g_appContext;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[Reset] distant: WiFi non connecté, reset direct"));
        return false;
    }

    if (g_appContext.otaManager.isUpdating()) {
        Serial.println(F("[Reset] distant: OTA déjà en cours, reset différé"));
        return true;
    }

    g_appContext.otaManager.setCurrentVersion(ProjectConfig::VERSION);
    if (!g_appContext.otaManager.checkForUpdate()) {
        Serial.println(F("[Reset] distant: aucune OTA disponible, reset direct"));
        return false;
    }

    if (g_appContext.otaManager.performUpdate()) {
        Serial.println(F("[Reset] distant: OTA disponible, mise à jour lancée avant reset"));
        return true;
    }

    Serial.println(F("[Reset] distant: OTA détectée mais échec démarrage update, reset direct"));
    return false;
}

void GPIOParser::resetEdgeDetectionState() {
    s_lastTankState = false;
    s_lastResetState = false;
    Serial.println(F("[GPIOParser] État détection de front réinitialisé"));
}

static bool parseBoolFromDoc(JsonVariantConst v) {
    // Dispatch de type via ArduinoJson ; conversion pure déléguée à GpioBoolParse.
    if (v.is<bool>()) return v.as<bool>();
    if (v.is<int>()) return GpioBoolParse::fromInt(v.as<int>());
    if (v.is<const char*>()) {
        bool out = false;
        if (GpioBoolParse::fromToken(v.as<const char*>(), out)) return out;
    }
    return false;
}

// v15.0: lecture d'un entier non signé depuis le doc serveur (le serveur envoie
// parfois des strings, ex. "42"). Valeurs négatives ramenées à 0.
static uint32_t parseUintFromDoc(JsonVariantConst v) {
    long val = 0;
    if (v.is<int>()) {
        val = v.as<int>();
    } else if (v.is<const char*>()) {
        const char* s = v.as<const char*>();
        val = s ? atol(s) : 0;
    } else {
        val = v.as<long>();
    }
    return (val < 0) ? 0u : static_cast<uint32_t>(val);
}

// v15.0: artefacts de l'ère « détection de front », conservés en no-op inoffensif
// (appelants dans automatism.cpp / feeding_finalize_orchestrator). Le protocole
// compteur monotone n'a plus d'état edge à aligner.
void GPIOParser::syncFeedEdgeStateAfterLocalPost(bool smallLevel, bool bigLevel) {
    (void)smallLevel;
    (void)bigLevel;
}

void GPIOParser::noteLocalFeedTriggered(bool isBig) {
    (void)isBig;
}

// Pré-passage UNIQUE des commandes de nourrissage 108/109 (avant la boucle GPIO).
// v15.0: protocole COMPTEUR MONOTONE. 108/109 sont des entiers = total de commandes
// jamais émises pour le canal. On compare au compteur exécuté persisté (NVS) et on
// distribue au plus un repas par poll et par canal (cap MAX_FEED_CATCHUP). Les cas
// FEED_SMALL/FEED_BIG de applyGPIO sont neutralisés (no-op).
static void resolveFeedCommands(const JsonDocument& doc, Automatism& autoCtrl) {
    auto readChannel = [&](const GPIOMapping& m, bool& present) -> uint32_t {
        char keyNum[16];
        snprintf(keyNum, sizeof(keyNum), "%d", m.gpio);
        const char* key = nullptr;
        if (doc.containsKey(keyNum)) {
            key = keyNum;
        } else if (m.serverPostName && doc.containsKey(m.serverPostName)) {
            key = m.serverPostName;
        }
        present = (key != nullptr);
        return present ? parseUintFromDoc(doc[key]) : 0u;
    };

    FeedingCommandResolver::Inputs in;
    in.smallCounter = readChannel(GPIOMap::FEED_SMALL, in.smallPresent);
    in.bigCounter   = readChannel(GPIOMap::FEED_BIG, in.bigPresent);
    if (!in.smallPresent && !in.bigPresent) {
        return;  // aucune clé nourrissage dans ce poll
    }
    in.execSmall = loadFeedExec(NVSKeys::Config::FEED_EXEC_SMALL);
    in.execBig   = loadFeedExec(NVSKeys::Config::FEED_EXEC_BIG);
    in.seeded    = loadFeedSeeded();
    in.feedingInProgress = autoCtrl.isFeedingInProgress();

    const FeedingCommandResolver::Decision d = FeedingCommandResolver::resolve(in);

    Serial.printf("[DBG] resolveFeedCommands small(p=%d,c=%lu) big(p=%d,c=%lu) "
                  "exec(s=%lu,b=%lu) seeded=%d inProg=%d -> action=%d newExec(s=%lu,b=%lu) persist=%d\n",
                  (int)in.smallPresent, (unsigned long)in.smallCounter,
                  (int)in.bigPresent, (unsigned long)in.bigCounter,
                  (unsigned long)in.execSmall, (unsigned long)in.execBig,
                  (int)in.seeded, (int)in.feedingInProgress, static_cast<int>(d.action),
                  (unsigned long)d.newExecSmall, (unsigned long)d.newExecBig, (int)d.persist);

    switch (d.action) {
        case FeedingCommandResolver::Action::Both:
            Serial.println(F("[GPIOParser] Nourrissage GROS+PETITS (compteur) -> cycle séquentiel"));
            autoCtrl.manualFeedBoth();
            break;
        case FeedingCommandResolver::Action::Big:
            Serial.println(F("Nourrissage gros (compteur monotone)"));
            autoCtrl.manualFeedBig();
            break;
        case FeedingCommandResolver::Action::Small:
            Serial.println(F("Nourrissage petits (compteur monotone)"));
            autoCtrl.manualFeedSmall();
            break;
        case FeedingCommandResolver::Action::None:
            if (in.feedingInProgress &&
                ((in.smallPresent && in.smallCounter > in.execSmall) ||
                 (in.bigPresent && in.bigCounter > in.execBig))) {
                Serial.println(F("[GPIOParser] Nourrissage différé (cycle en cours) - re-tenté au prochain poll"));
            }
            break;
    }

    // Persister les nouveaux compteurs exécutés + flag amorçage (NVS write-if-changed).
    if (d.persist) {
        if (d.newExecSmall != in.execSmall) {
            saveFeedExec(NVSKeys::Config::FEED_EXEC_SMALL, d.newExecSmall);
        }
        if (d.newExecBig != in.execBig) {
            saveFeedExec(NVSKeys::Config::FEED_EXEC_BIG, d.newExecBig);
        }
        if (d.newSeeded != in.seeded) {
            saveFeedSeeded(d.newSeeded);
        }
    }
}

void GPIOParser::seedFeedStateFromDoc(const JsonDocument& doc) {
    // Reset (110): seed pour ne pas redémarrer si le sync envoie 110:1 comme état courant.
    if (doc.containsKey("110")) {
        s_lastResetState = parseBoolFromDoc(doc["110"]);
    }
    // v15.0: amorçage compteur nourrissage 108/109 au 1er poll. Si le flag feedSeed
    // est encore false (flash neuf), on adopte les compteurs serveur comme « exécutés »
    // SANS distribuer, puis on pose feedSeed=true. Les polls suivants ne déclencheront
    // que sur un vrai incrément serverCounter > executed.
    if (!loadFeedSeeded()) {
        bool smallPresent = false, bigPresent = false;
        uint32_t smallCounter = 0, bigCounter = 0;
        {
            char keyNum[16];
            snprintf(keyNum, sizeof(keyNum), "%d", GPIOMap::FEED_SMALL.gpio);
            const char* k = doc.containsKey(keyNum) ? keyNum
                            : (GPIOMap::FEED_SMALL.serverPostName &&
                               doc.containsKey(GPIOMap::FEED_SMALL.serverPostName)
                                   ? GPIOMap::FEED_SMALL.serverPostName : nullptr);
            if (k) { smallPresent = true; smallCounter = parseUintFromDoc(doc[k]); }
        }
        {
            char keyNum[16];
            snprintf(keyNum, sizeof(keyNum), "%d", GPIOMap::FEED_BIG.gpio);
            const char* k = doc.containsKey(keyNum) ? keyNum
                            : (GPIOMap::FEED_BIG.serverPostName &&
                               doc.containsKey(GPIOMap::FEED_BIG.serverPostName)
                                   ? GPIOMap::FEED_BIG.serverPostName : nullptr);
            if (k) { bigPresent = true; bigCounter = parseUintFromDoc(doc[k]); }
        }
        if (smallPresent || bigPresent) {
            if (smallPresent) saveFeedExec(NVSKeys::Config::FEED_EXEC_SMALL, smallCounter);
            if (bigPresent)   saveFeedExec(NVSKeys::Config::FEED_EXEC_BIG, bigCounter);
            saveFeedSeeded(true);
            Serial.printf("[GPIOParser] Amorçage compteur nourrissage: execSmall=%lu execBig=%lu (seeded)\n",
                          (unsigned long)smallCounter, (unsigned long)bigCounter);
        }
    }
    Serial.printf("[GPIOParser] État seed initialisé: reset=%d feedSeeded=%d\n",
                  s_lastResetState ? 1 : 0, loadFeedSeeded() ? 1 : 0);
}

void GPIOParser::parseAndApply(const JsonDocument& doc, Automatism& autoCtrl) {
    Serial.println(F("[GPIOParser] === PARSING JSON SERVEUR ==="));
    // Déclenchement OTA depuis serveur distant (page contrôle prod/test) : triggerOtaCheck = true
    if (doc.containsKey("triggerOtaCheck") && doc["triggerOtaCheck"].as<bool>()) {
        AppTasks::netRequestOtaCheck();
        Serial.println(F("[GPIOParser] triggerOtaCheck reçu: demande vérification OTA"));
    }
    // Pré-passage nourrissage (108/109) AVANT la boucle GPIO : traite petits/gros
    // ensemble pour gérer le cas simultané (cycle séquentiel) et ne perdre aucune
    // commande. La boucle ci-dessous neutralise FEED_SMALL/FEED_BIG dans applyGPIO.
    resolveFeedCommands(doc, autoCtrl);
  size_t presentKeys = 0;
    // v11.189: Seed reset (110) géré uniquement par seedFeedStateFromDoc au 1er poll
    // v11.192: Réactiver reset via serveur distant - edge detection dans applyGPIO
    // v11.78: Collecter les GPIO virtuels pour application immédiate
    StaticJsonDocument<1024> configDoc;  // Augmenté de 512 à 1024 pour éviter les dépassements
    bool hasVirtualConfig = false;
    
    // Parcourir tous les GPIO définis dans le mapping
    // v11.191: Accepter clé numérique ("16") OU symbolique (serverPostName, ex: "etatPompeAqua")
    // pour prise en compte des commandes serveur distant quel que soit le format renvoyé
    for (size_t i = 0; i < GPIOMap::MAPPING_COUNT; i++) {
        const GPIOMapping& mapping = GPIOMap::ALL_MAPPINGS[i];
        char keyNum[16];
        snprintf(keyNum, sizeof(keyNum), "%d", mapping.gpio);

        const char* keyUsed = nullptr;
        if (doc.containsKey(keyNum)) {
            keyUsed = keyNum;
        } else if (mapping.serverPostName && doc.containsKey(mapping.serverPostName)) {
            keyUsed = mapping.serverPostName;
        }
        if (!keyUsed) {
            continue;
        }
        presentKeys++;

        JsonVariantConst value = doc[keyUsed];
        Serial.printf("[GPIOParser] GPIO %d (%s): ", mapping.gpio, mapping.description);
        if (value.is<int>()) {
            Serial.printf("[GPIO] key=%s raw=%d\n", keyUsed, value.as<int>());
        } else if (value.is<float>()) {
            Serial.printf("[GPIO] key=%s raw=%.2f\n", keyUsed, value.as<float>());
        } else if (value.is<const char*>()) {
            const char* valueStr = value.as<const char*>();
            Serial.printf("[GPIO] key=%s raw=%s\n", keyUsed, valueStr ? valueStr : "");
        } else {
            Serial.printf("[GPIO] key=%s raw=\n", keyUsed);
        }
        // v11.192: Reset (110) réactivé via serveur - edge detection 0→1 dans applyGPIO
        applyGPIO(mapping.gpio, value, autoCtrl, configDoc, hasVirtualConfig);
        saveToNVS(mapping, value);
    }

    // v11.98: Traiter aussi les clés textuelles (compatibilité avec /dbvars/update et autres sources)
    // Mapping des clés textuelles vers les clés de configuration
    const char* textKeys[] = {
        "aqThreshold", "tankThreshold", "chauffageThreshold", 
        "limFlood", "tempsRemplissageSec", "refillDuration",
        "tempsGros", "tempsPetits",
        "bouffeMatin", "bouffeMidi", "bouffeSoir",
        "mail", "mailNotif", "FreqWakeUp", "WakeUp", "forceWakeUp"
    };
    
    for (size_t i = 0; i < sizeof(textKeys) / sizeof(textKeys[0]); i++) {
        const char* textKey = textKeys[i];
        if (doc.containsKey(textKey)) {
            // Vérifier que cette clé n'a pas déjà été traitée (numérique ou symbolique)
            bool alreadyProcessed = false;
            for (size_t j = 0; j < GPIOMap::MAPPING_COUNT; j++) {
                const GPIOMapping& mapping = GPIOMap::ALL_MAPPINGS[j];
                char gpioKey[16];
                snprintf(gpioKey, sizeof(gpioKey), "%d", mapping.gpio);
                bool hasNum = doc.containsKey(gpioKey);
                bool hasSym = mapping.serverPostName && doc.containsKey(mapping.serverPostName);
                if (hasNum || hasSym) {
                    JsonVariantConst v = hasNum ? doc[gpioKey] : doc[mapping.serverPostName];
                    const char* mappedKey = mapGPIOToConfigKey(mapping.gpio, v);
                    if (mappedKey && strcmp(mappedKey, textKey) == 0) {
                        alreadyProcessed = true;
                        break;
                    }
                }
            }
            
            if (!alreadyProcessed) {
                // Ajouter à configDoc pour application
                JsonVariantConst value = doc[textKey];
                // Limites config serveur distant (GPIO virtuels + clés textuelles)
                if (configDoc.size() < 28 && configDoc.memoryUsage() < 1200) {
                    configDoc[textKey] = value;
                    hasVirtualConfig = true;
                    Serial.printf("[GPIOParser] Clé textuelle '%s' ajoutée au document de config\n", textKey);
                }
            }
        }
    }
    
    // v11.78: Appliquer immédiatement les configurations virtuelles avec protection
    if (hasVirtualConfig) {
        Serial.println(F("[GPIOParser] 🔧 Application immédiate des GPIO virtuels"));
        Serial.printf("[GPIOParser] Document JSON: %u éléments, taille: %u bytes\n", 
                     configDoc.size(), configDoc.memoryUsage());
        
        // Limites config complète serveur distant (incl. angles servo GPIO 118-123)
        if (configDoc.size() > 0 && configDoc.size() < 30 && configDoc.memoryUsage() < 1300) {
            // Vérification supplémentaire de la mémoire disponible
            size_t freeHeap = ESP.getFreeHeap();
            if (freeHeap > 40000) { // Minimum 40KB de heap libre (réduit de 50KB)
                Serial.printf("[GPIOParser] Heap libre: %u bytes - Application sécurisée\n", freeHeap);
                autoCtrl.applyConfigFromJson(configDoc);
                Serial.println(F("[GPIOParser] ✅ Configurations virtuelles appliquées"));
            } else {
                Serial.printf("[GPIOParser] ⚠️ Heap insuffisant (%u bytes), application reportée\n", freeHeap);
            }
        } else {
            Serial.printf("[GPIOParser] ⚠️ Document JSON invalide (size:%u, mem:%u bytes), ignoré\n", 
                         configDoc.size(), configDoc.memoryUsage());
        }
    }
    
  Serial.printf("[GPIOParser] Présents: %u / %u\n", (unsigned)presentKeys, (unsigned)GPIOMap::MAPPING_COUNT);
  if (presentKeys > 0 || hasVirtualConfig) {
    Serial.printf("[GPIOParser] Config appliquée (RAM+NVS), clés: %u\n", (unsigned)presentKeys);
  }
  Serial.println(F("[GPIOParser] === FIN PARSING ==="));
}

void GPIOParser::applyGPIO(uint8_t gpio, JsonVariantConst value, Automatism& autoCtrl, 
                           JsonDocument& configDoc, bool& hasVirtualConfig) {
    // Actionneurs physiques
    if (gpio == GPIOMap::PUMP_AQUA.gpio) {
        bool state = parseBool(value);
        state ? autoCtrl.startAquaPumpManualLocal() : autoCtrl.stopAquaPumpManualLocal();
        Serial.printf("Pompe aqua %s\n", state ? "ON" : "OFF");
    }
    else if (gpio == GPIOMap::PUMP_TANK.gpio) {
        // v11.179: Utilise variable module-level pour reset au boot
        bool state = parseBool(value);
        if (state && !s_lastTankState) {
            autoCtrl.startTankPumpManual();
            Serial.println("Pompe tank ON (front montant)");
        } else if (!state && s_lastTankState) {
            autoCtrl.stopTankPumpManual();
            Serial.println("Pompe tank OFF (front descendant)");
        } else {
            Serial.printf("Pompe tank %s (commande redondante)\n", state ? "ON" : "OFF");
        }
        s_lastTankState = state;
    }
    else if (gpio == GPIOMap::HEATER.gpio) {
        bool state = parseBool(value);
        state ? autoCtrl.startHeaterManualLocal() : autoCtrl.stopHeaterManualLocal();
        Serial.printf("Chauffage %s\n", state ? "ON" : "OFF");
    }
    else if (gpio == GPIOMap::LIGHT.gpio) {
        bool state = parseBool(value);
        state ? autoCtrl.startLightManualLocal() : autoCtrl.stopLightManualLocal();
        Serial.printf("Lumière %s\n", state ? "ON" : "OFF");
    }
    // Relais auxiliaires (v15.13) : commande directe ON/OFF
    else if (gpio == GPIOMap::AUX1.gpio || gpio == GPIOMap::AUX2.gpio) {
        bool state = parseBool(value);
        uint8_t which = (gpio == GPIOMap::AUX1.gpio) ? 1 : 2;
        autoCtrl.setAuxManualLocal(which, state);
        Serial.printf("Relais AUX%u %s\n", which, state ? "ON" : "OFF");
    }
    // Nourrissage 108/109 : traité en amont par resolveFeedCommands (pré-passage,
    // gère le cas petits+gros SIMULTANÉS et la non-perte des commandes). No-op ici
    // pour éviter tout double déclenchement et préserver l'état edge déjà résolu.
    else if (gpio == GPIOMap::FEED_SMALL.gpio || gpio == GPIOMap::FEED_BIG.gpio) {
        (void)value;
    }
    // Reset (GPIO 110): front montant uniquement - serveur distant ou seed 1er poll évite reboot en boucle
    else if (gpio == GPIOMap::RESET_CMD.gpio) {
        bool state = parseBool(value);
        if (state && !s_lastResetState) {
            s_lastResetState = true;
            Serial.println(F("Reset demandé"));
            Serial.println(F("[Sync] Reboot distant exécuté (GPIO 110=1)"));
            if (!tryStartOtaBeforeResetFromRemote()) {
                // v14.17 : reboot délibéré → valider l'image OTA en probation (évite un
                // rollback d'une bonne image sur un reboot distant volontaire).
                SystemBoot::confirmOtaValidation("reboot distant (GPIO 110)");
                ESP.restart();
            }
        } else if (!state) {
            s_lastResetState = false;
        }
    }
    // Configuration - appliquée via setters Automatism
    else {
        const char* valueStr = value.is<const char*>() ? value.as<const char*>() : "";
        Serial.printf("Config GPIO %d = %s\n", gpio, valueStr);
        
        // v11.77: Appliquer immédiatement les configurations virtuelles
        const GPIOMapping* mapping = GPIOMap::findByGPIO(gpio);
        if (mapping && (mapping->type == GPIOType::CONFIG_INT || 
                        mapping->type == GPIOType::CONFIG_FLOAT || 
                        mapping->type == GPIOType::CONFIG_STRING || 
                        mapping->type == GPIOType::CONFIG_BOOL)) {
            
            // Mapper GPIO vers clés attendues par applyConfigFromJson
            const char* configKey = mapGPIOToConfigKey(gpio, value);
            if (configKey && strlen(configKey) > 0) {
                // v11.176: Limites augmentées (20 clés, 950 bytes) pour config complète serveur distant
                // Vérifier que le document n'est pas plein avant d'ajouter
                if (configDoc.size() < 28 && configDoc.memoryUsage() < 1200) {
                    // Utiliser directement la clé const char*
                    const char* keyStr = configKey;
                    
                    // v11.80: Conversion selon le type attendu du GPIO (pas le type reçu)
                    // Cela permet de gérer les cas où le serveur envoie des strings au lieu de nombres
                    bool assignmentOk = false;
                    
                    // Convertir selon le type attendu du GPIO, pas le type reçu
                    if (mapping->type == GPIOType::CONFIG_INT) {
                        // v11.183: Serveur ffp3 envoie souvent des strings ("200", "9"); as<int>() retourne 0 sur string
                        int intVal = value.is<const char*>()
                            ? atoi(value.as<const char*>())
                            : value.as<int>();
                        
                        // Validation heures nourrissage (GPIO 105, 106, 107)
                        if (gpio == 105 || gpio == 106 || gpio == 107) {
                            if (intVal < 0 || intVal > 23) {
                                Serial.printf("[GPIOParser] ⚠️ Heure invalide GPIO %d: %d (doit être 0-23), ignorée\n", 
                                             gpio, intVal);
                                return; // Ignorer la valeur invalide
                            }
                        }
                        // Validation angles servo nourrissage (GPIO 118-123)
                        if (gpio >= 118 && gpio <= 123) {
                            if (intVal < 0 || intVal > 180) {
                                Serial.printf("[GPIOParser] ⚠️ Angle servo invalide GPIO %d: %d (doit être 0-180), ignorée\n",
                                             gpio, intVal);
                                return;
                            }
                        }
                        
                        configDoc[keyStr] = intVal;
                        assignmentOk = true;
                        Serial.printf("[GPIOParser] GPIO %d (INT) -> %s = %d (converti depuis %s)\n", 
                                     gpio, keyStr, intVal, valueStr);
                    }
                    else if (mapping->type == GPIOType::CONFIG_FLOAT) {
                        // v11.183: Serveur envoie parfois string ("20"); as<float>() retourne 0 sur string
                        float floatVal = value.is<const char*>()
                            ? static_cast<float>(atof(value.as<const char*>()))
                            : value.as<float>();
                        configDoc[keyStr] = floatVal;
                        assignmentOk = true;
                        Serial.printf("[GPIOParser] GPIO %d (FLOAT) -> %s = %.2f (converti depuis %s)\n", 
                                     gpio, keyStr, floatVal, valueStr);
                    }
                    else if (mapping->type == GPIOType::CONFIG_BOOL) {
                        // Pour CONFIG_BOOL, utiliser parseBool qui gère les strings
                        bool boolVal = parseBool(value);
                        configDoc[keyStr] = boolVal;
                        assignmentOk = true;
                        Serial.printf("[GPIOParser] GPIO %d (BOOL) -> %s = %s (converti depuis %s)\n", 
                                     gpio, keyStr, boolVal ? "true" : "false", valueStr);
                    }
                    else if (mapping->type == GPIOType::CONFIG_STRING) {
                        // Pour CONFIG_STRING, vérifier que c'est bien une string valide
                        if (value.is<const char*>()) {
                            const char* valPtr = value.as<const char*>();
                            if (valPtr != nullptr && strlen(valPtr) < 100) { // Limite de taille
                                configDoc[keyStr] = valPtr;
                                assignmentOk = true;
                                Serial.printf("[GPIOParser] GPIO %d (STRING) -> %s = '%s'\n", 
                                             gpio, keyStr, valPtr);
                            } else {
                                Serial.printf("[GPIOParser] ⚠️ String invalide GPIO %d, ignoré\n", gpio);
                            }
                        } else {
                            // Si ce n'est pas une string, convertir en string via buffer temporaire
                            char tempStr[256];
                            if (value.is<const char*>()) {
                                const char* str = value.as<const char*>();
                                if (str) {
                                    strncpy(tempStr, str, sizeof(tempStr) - 1);
                                    tempStr[sizeof(tempStr) - 1] = '\0';
                                } else {
                                    tempStr[0] = '\0';
                                }
                            } else {
                                // Conversion via snprintf pour les types numériques
                                if (value.is<int>()) {
                                    snprintf(tempStr, sizeof(tempStr), "%d", value.as<int>());
                                } else if (value.is<float>()) {
                                    snprintf(tempStr, sizeof(tempStr), "%.2f", value.as<float>());
                                } else if (value.is<bool>()) {
                                    strncpy(tempStr, value.as<bool>() ? "true" : "false", sizeof(tempStr) - 1);
                                    tempStr[sizeof(tempStr) - 1] = '\0';
                                } else {
                                    tempStr[0] = '\0';
                                }
                            }
                            configDoc[keyStr] = tempStr;
                            assignmentOk = true;
                                Serial.printf("[GPIOParser] GPIO %d (STRING) -> %s = '%s' (converti)\n", 
                                         gpio, keyStr, tempStr);
                        }
                    }
                    else {
                        Serial.printf("[GPIOParser] ⚠️ Type GPIO non géré: %d pour GPIO %d\n", 
                                     (int)mapping->type, gpio);
                    }
                    
                    if (assignmentOk) {
                        hasVirtualConfig = true;
                    }
                } else {
                    Serial.printf("[GPIOParser] ⚠️ Document JSON plein (size:%u, mem:%u), GPIO virtuel %d ignoré\n", 
                                 configDoc.size(), configDoc.memoryUsage(), gpio);
                }
            }
        }
    }
}

const char* GPIOParser::mapGPIOToConfigKey(uint8_t gpio, JsonVariantConst value) {
    // Mapper les GPIO virtuels vers les clés attendues par applyConfigFromJson
    switch (gpio) {
        case 100: // EMAIL_ADDR
            return "mail";
        case 101: // EMAIL_EN  
            return "mailNotif";
        case 102: // AQ_THRESHOLD
            return "aqThreshold";
        case 103: // TANK_THRESHOLD
            return "tankThreshold";
        case 104: // HEAT_THRESHOLD
            return "chauffageThreshold";
        case 105: // FEED_MORNING
            return "bouffeMatin";
        case 106: // FEED_NOON
            return "bouffeMidi";
        case 107: // FEED_EVENING
            return "bouffeSoir";
        case 111: // FEED_BIG_DUR
            return "tempsGros";
        case 112: // FEED_SMALL_DUR
            return "tempsPetits";
        case 113: // REFILL_DUR
            return "refillDuration";
        case 114: // LIM_FLOOD
            return "limFlood";
        case 115: // WAKEUP
            return "forceWakeUp";
        case 116: // FREQ_WAKEUP
            return "FreqWakeUp";
        case 118:
            return "angleReposGros";
        case 119:
            return "angleDistribGros";
        case 120:
            return "angleInterGros";
        case 121:
            return "angleReposPetits";
        case 122:
            return "angleDistribPetits";
        case 123:
            return "angleInterPetits";
        default:
            return nullptr;
    }
}

void GPIOParser::saveToNVS(const GPIOMapping& mapping, JsonVariantConst value) {
    // v11.172: Seuls les ACTUATORS sont sauvegardés individuellement en NVS
    // Les CONFIG_* sont centralisés dans remote_json (source de vérité unique)
    if (mapping.type != GPIOType::ACTUATOR) {
        // Skip - config values are in remote_json
        return;
    }
    
    // Créer la clé préfixée
    char key[32];
    snprintf(key, sizeof(key), "gpio_%s", mapping.nvsKey);

    // Clé complète = "gpio_" (5 car.) + nvsKey ; limite NVS = 15 car.
    if (mapping.nvsKey == nullptr || strlen(mapping.nvsKey) == 0 || strlen(mapping.nvsKey) > 10) {
        Serial.printf("[NVS] ⚠️ Clé NVS invalide, ignorée: %s\n", mapping.nvsKey ? mapping.nvsKey : "NULL");
        return;
    }
    
    // v11.172: Seulement les actionneurs (bool) sont sauvegardés ici
    bool newVal = parseBool(value);
    bool currentVal = false;
    // Défaut = !newVal : si la clé est absente, currentVal != newVal -> la 1re écriture
    // persiste (sinon défaut==newVal masquait le tout premier enregistrement).
    g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, key, currentVal, !newVal);
    if (currentVal != newVal) {
        g_nvsManager.saveBool(NVS_NAMESPACES::CONFIG, key, newVal);
        Serial.printf("[NVS] ✏️ Actuateur %s = %s\n", key, newVal ? "ON" : "OFF");
    }
}


void GPIOParser::loadGPIOStatesFromNVS(Automatism& autoCtrl) {
    Serial.println(F("[GPIOParser] Chargement des états GPIO depuis NVS..."));
    
    // Charger les états des actionneurs depuis NVS
    for (size_t i = 0; i < GPIOMap::MAPPING_COUNT; i++) {
        const GPIOMapping& mapping = GPIOMap::ALL_MAPPINGS[i];
        
        // Seulement les actionneurs (type ACTUATOR)
        if (mapping.type != GPIOType::ACTUATOR) {
            continue;
        }
        
        // Clé complète = "gpio_" (5 car.) + nvsKey ; limite NVS = 15 car.
        if (mapping.nvsKey == nullptr || strlen(mapping.nvsKey) == 0 || strlen(mapping.nvsKey) > 10) {
            continue;
        }
        
        char key[32];
        snprintf(key, sizeof(key), "gpio_%s", mapping.nvsKey);

        bool state;
        g_nvsManager.loadBool(NVS_NAMESPACES::CONFIG, key, state, false);
        
        // Appliquer l'état selon le GPIO
        if (mapping.gpio == GPIOMap::HEATER.gpio) {
            if (state) {
                autoCtrl.startHeaterManualLocal();
                Serial.printf("[GPIOParser] 🔥 Chauffage restauré: ON\n");
            } else {
                autoCtrl.stopHeaterManualLocal();
                Serial.printf("[GPIOParser] 🔥 Chauffage restauré: OFF\n");
            }
        }
        else if (mapping.gpio == GPIOMap::AUX1.gpio || mapping.gpio == GPIOMap::AUX2.gpio) {
            uint8_t which = (mapping.gpio == GPIOMap::AUX1.gpio) ? 1 : 2;
            autoCtrl.setAuxManualLocal(which, state);
            Serial.printf("[GPIOParser] 🔌 Relais AUX%u restauré: %s\n", which, state ? "ON" : "OFF");
        }
        else if (mapping.gpio == GPIOMap::LIGHT.gpio) {
            if (state) {
                autoCtrl.startLightManualLocal();
                Serial.printf("[GPIOParser] 💡 Lumière restaurée: ON\n");
            } else {
                autoCtrl.stopLightManualLocal();
                Serial.printf("[GPIOParser] 💡 Lumière restaurée: OFF\n");
            }
        }
        else if (mapping.gpio == GPIOMap::PUMP_AQUA.gpio) {
            if (state) {
                autoCtrl.startAquaPumpManualLocal();
                Serial.printf("[GPIOParser] 💧 Pompe aqua restaurée: ON\n");
            } else {
                autoCtrl.stopAquaPumpManualLocal();
                Serial.printf("[GPIOParser] 💧 Pompe aqua restaurée: OFF\n");
            }
        }
        // Note: PUMP_TANK volontairement exclue pour sécurité
    }
    
    Serial.println(F("[GPIOParser] ✅ États GPIO chargés depuis NVS"));
}

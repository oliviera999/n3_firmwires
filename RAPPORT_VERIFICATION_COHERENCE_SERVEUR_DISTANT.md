# Rapport de Vérification - Cohérence Serveur Distant

**Date:** 2026-01-26  
**Version ESP32:** 11.156  
**Version Serveur:** FFP3 (Slim Framework)

## Résumé Exécutif

✅ **Le code permettant les échanges avec le serveur distant est globalement cohérent.**

Tous les points critiques ont été vérifiés et sont compatibles entre l'ESP32 et le serveur. Quelques points d'attention ont été identifiés pour la configuration en production.

---

## 1. Endpoints et URLs ✅

### Configuration Serveur
- **BasePath Slim:** Calculé automatiquement via `dirname(dirname($_SERVER['SCRIPT_NAME']))`
  - Si script dans `/ffp3/public/index.php` → basePath = `/ffp3`
- **Routes définies:**
  - PROD: `/post-data`, `/api/outputs/state`, `/heartbeat`
  - TEST: `/post-data-test`, `/api/outputs-test/state`, `/heartbeat-test`

### Configuration ESP32
- **BASE_URL:** `https://iot.olution.info`
- **Endpoints:**
  - PROD: `/ffp3/post-data`, `/ffp3/api/outputs/state`, `/ffp3/heartbeat`
  - TEST: `/ffp3/post-data-test`, `/ffp3/api/outputs-test/state`, `/ffp3/heartbeat-test`

### Vérification
✅ **Cohérent:** Les URLs complètes correspondent (basePath + route = endpoint ESP32)

**Note:** Le calcul automatique du basePath dépend de la structure des dossiers sur le serveur. À vérifier en production que `$_SERVER['SCRIPT_NAME']` contient bien le chemin attendu.

---

## 2. Champs POST - Envoi Données Capteurs ✅

### ESP32 (v11.141+)
**Champs envoyés:**
- `version`, `TempAir`, `Humidite`, `TempEau`
- `EauPotager`, `EauAquarium`, `EauReserve`
- `diffMaree`, `Luminosite`
- `etatPompeAqua`, `etatPompeTank`, `etatHeat`, `etatUV`
- `resetMode` (optionnel)

⚠️ **Note v11.141:** Les configurations (seuils, durées, heures) ne sont **PLUS envoyées** par l'ESP32.

### Serveur
**Champs attendus (tous optionnels):**
- Tous les champs ci-dessus
- Configurations optionnelles: `bouffeMatin`, `bouffeMidi`, `bouffeSoir`, `bouffePetits`, `bouffeGros`
- `aqThreshold`, `tankThreshold`, `chauffageThreshold`
- `mail`, `mailNotif`
- `tempsGros`, `tempsPetits`, `tempsRemplissageSec`, `limFlood`, `wakeUp`, `freqWakeUp`

### Traitement Serveur
Le serveur utilise `$toInt()` et `$sanitize()` qui retournent `null` si le champ est absent. La méthode `syncStatesFromSensorData()` ne met à jour les GPIO que si `$value !== null`.

✅ **Cohérent:** Le serveur gère correctement l'absence des champs de configuration.

**Impact:** Les configurations ne seront plus synchronisées depuis l'ESP32 vers le serveur (comportement voulu selon v11.141).

---

## 3. Format Réponse GET - États GPIO ✅

### Serveur
**Format JSON:**
```json
{"2":"1","15":"0","16":"1","18":"0","100":"oliv.arn.lau@gmail.com","101":"checked",...}
```

**GPIOs retournés:** 21 GPIOs (2, 15, 16, 18, 100-116)

### ESP32
- **Buffer JSON:** `BufferConfig::JSON_DOCUMENT_SIZE` = 1024 bytes (wroom)
- **Parsing:** `deserializeJson(doc, payloadBuffer)`

### Estimation Taille
- Format: `"gpio":"state",` ≈ 8-12 caractères par GPIO
- 21 GPIOs × 10 caractères = ~210 caractères
- Total estimé: ~220-250 bytes

✅ **Cohérent:** La taille de la réponse est bien en dessous de la limite de 1024 bytes.

---

## 4. Heartbeat ✅

### ESP32
**Payload:**
```
uptime={uptime}&free={free}&min={min}&reboots={reboots}&crc={crc32}
```

**CRC32:**
- Polynôme: `0xEDB88320` (IEEE 802.3 CRC-32)
- Calcul: `~crc` (complément à 1)
- Format: Hexadécimal majuscule (`%08lX`)

### Serveur
**Validation:**
- Chaîne: `"uptime={uptime}&free={free}&min={min}&reboots={reboots}"`
- Algorithme: `hash('crc32b', $raw)`
- Format: Hexadécimal majuscule (`strtoupper()`)

### Vérification
✅ **Cohérent:** 
- Même polynôme (0xEDB88320)
- Même format de sortie (hex majuscule)
- Même chaîne de calcul (sans le champ `crc`)

---

## 5. Authentification ✅

### ESP32
- **Clé API:** `"fdGTMoptd5CD2ert3"` (hardcodée dans `include/config.h`)
- **Ajout automatique:** `api_key` et `sensor` ajoutés si absents dans le payload

### Serveur
- **Source:** Variable d'environnement `$_ENV['API_KEY']`
- **Validation:** Comparaison stricte avec la clé fournie

### Vérification
✅ **Cohérent:** La valeur de référence est identique (`fdGTMoptd5CD2ert3`).

⚠️ **Point d'attention:** La clé API sur le serveur doit être configurée dans le fichier `.env` avec la même valeur. À vérifier en production.

---

## 6. Content-Type et Méthodes HTTP ✅

### ESP32
- **POST:** `Content-Type: application/x-www-form-urlencoded`
- **GET:** Pas de Content-Type spécifique

### Serveur
- **POST:** Accepte `application/x-www-form-urlencoded` (parsed body)
- **GET:** Renvoie `Content-Type: application/json`

✅ **Cohérent:** Formats compatibles.

---

## Points d'Attention

### 1. BasePath Serveur
Le calcul automatique du basePath dépend de la structure des dossiers. À vérifier en production que le chemin est correct.

**Recommandation:** Tester manuellement les endpoints en production pour confirmer que le basePath est bien `/ffp3`.

### 2. Clé API Environnement
La clé API est lue depuis une variable d'environnement sur le serveur. À vérifier que `API_KEY=fdGTMoptd5CD2ert3` est bien configurée dans le `.env` du serveur.

**Recommandation:** Vérifier la configuration `.env` en production.

### 3. Champs POST Configuration
Depuis v11.141, l'ESP32 n'envoie plus les champs de configuration. Le serveur les gère correctement (null si absents), mais ils ne seront plus synchronisés depuis l'ESP32.

**Impact:** Les configurations doivent être modifiées uniquement via l'interface web serveur, pas depuis l'ESP32.

---

## Conclusion

✅ **Le code est cohérent** entre l'ESP32 et le serveur distant pour tous les points critiques vérifiés:

1. ✅ Endpoints et URLs
2. ✅ Champs POST (avec gestion correcte des valeurs null)
3. ✅ Format réponse JSON (taille compatible)
4. ✅ Heartbeat CRC32 (même algorithme)
5. ✅ Authentification (même clé API de référence)
6. ✅ Content-Type et méthodes HTTP

**Actions recommandées:**
1. Vérifier le calcul du basePath en production
2. Confirmer la configuration de `API_KEY` dans le `.env` serveur
3. Documenter le changement de comportement v11.141 (configs non envoyées)

Le système de communication est prêt pour la production, avec quelques vérifications de configuration à effectuer.

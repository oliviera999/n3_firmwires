# Vérification des Endpoints ESP32 ↔ Serveur

Date : 2025-10-08

## 📡 Configuration ESP32

### URLs utilisées par l'ESP32
Depuis `include/project_config.h`:

**Production:**
```cpp
BASE_URL = "http://iot.olution.info"
POST_DATA_ENDPOINT = "/ffp3/ffp3datas/post-data"
OUTPUT_ENDPOINT = "/ffp3/ffp3datas/api/outputs/states/1"
```

**Test:**
```cpp
POST_DATA_ENDPOINT = "/ffp3/ffp3datas/post-data-test"
OUTPUT_ENDPOINT = "/ffp3/ffp3datas/api/outputs-test/states/1"
```

### Méthodes utilisées
- **POST vers post-data**: Envoie des mesures de capteurs via `application/x-www-form-urlencoded`
- **GET vers api/outputs/states/{board}**: Récupère l'état des GPIO

---

## 🖥️ Configuration Serveur

### Routes définies dans `public/index.php`

**BasePath Slim:**
```php
$basePath = str_replace('\\', '/', dirname(dirname($_SERVER['SCRIPT_NAME'])));
// Devrait donner: /ffp3/ffp3datas
```

**Routes Production:**
```php
POST /post-data → PostDataController::handle()
POST /post-ffp3-data.php → PostDataController::handle() (alias legacy)
GET  /api/outputs/states/{board} → OutputController::getStatesApi()
```

**Routes Test:**
```php
POST /post-data-test → PostDataController::handle()
GET  /api/outputs-test/states/{board} → OutputController::getStatesApi()
```

---

## ✅ Points Validés

### 1. Endpoint POST-DATA
- ✅ ESP32 envoie vers: `http://iot.olution.info/ffp3/ffp3datas/post-data`
- ✅ Serveur écoute sur: `/post-data` (avec basePath `/ffp3/ffp3datas`)
- ✅ Format: `application/x-www-form-urlencoded` (compatible)
- ✅ Clé API vérifiée via `$_POST['api_key']`
- ✅ Tous les champs attendus sont présents

### 2. Endpoint OUTPUT-STATE
- ✅ ESP32 demande: `http://iot.olution.info/ffp3/ffp3datas/api/outputs/states/1`
- ✅ Serveur écoute sur: `/api/outputs/states/{board}`
- ✅ Format réponse: JSON `{"2":"1","3":"0",...}`
- ✅ Méthode: GET (correcte)

### 3. PostDataController
- ✅ Validation API_KEY via `$_POST['api_key']`
- ✅ Support signature HMAC (optionnel, fallback sur API_KEY)
- ✅ Tous les champs capteurs sont traités
- ✅ Gestion des erreurs appropriée

### 4. OutputController
- ✅ Retourne format JSON attendu
- ✅ Enregistre les requêtes des boards
- ✅ Gère les environnements PROD/TEST séparément

---

## ⚠️ PROBLÈMES DÉTECTÉS

### ✅ CORRIGÉ: esp32-compat.php (Lignes 32, 54)

**Problème détecté:** Les URLs utilisées dans `curl_init()` étaient RELATIVES et ne fonctionnaient PAS.

**Solution appliquée:** 
- Ajout de `$fullBaseUrl` qui construit l'URL complète avec protocole et domaine
- Utilisation de `$fullBaseUrl` dans tous les appels curl
- Ajout de timeout et gestion d'erreur améliorée

```php
// AVANT (INCORRECT)
$ch = curl_init($basePath . '/api/outputs/' . urlencode($id) . '/state');

// APRÈS (CORRIGÉ)
$fullBaseUrl = $protocol . '://' . $host . $basePath;
$targetUrl = $fullBaseUrl . '/api/outputs/' . urlencode($id) . '/state';
$ch = curl_init($targetUrl);
```

**Fichiers modifiés:**
- `ffp3/ffp3datas/public/esp32-compat.php` (lignes 15-18, 37-63, 70-92)

### ✅ VÉRIFIÉ: BasePath calculation (index.php ligne 24-33)

**Code vérifié:**
```php
$basePath = str_replace('\\', '/', dirname(dirname($_SERVER['SCRIPT_NAME'])));
if ($basePath !== '/' && $basePath !== '') {
    $app->setBasePath($basePath);
}
```

**Vérification:**
- Si `$_SERVER['SCRIPT_NAME']` = `/ffp3/ffp3datas/public/index.php`
- Alors `dirname(dirname(...))` = `/ffp3/ffp3datas` ✅
- Logique correcte pour structure actuelle

**Améliorations appliquées:**
- Ajout de commentaires explicatifs (lignes 25-26)
- Ajout d'une ligne de debug optionnelle (ligne 33)

**Fichiers modifiés:**
- `ffp3/ffp3datas/public/index.php` (lignes 24-33)

### 🟡 INFO: post-data.php (fichier standalone)

Le fichier `public/post-data.php` (lignes 1-203) semble être un doublon standalone qui n'utilise PAS le routing Slim. Il implémente sa propre logique.

**Question:** Ce fichier est-il encore utilisé ou est-ce un legacy ?
- Si utilisé: OK, mais redondant avec PostDataController
- Si legacy: Peut être supprimé pour éviter confusion

---

## ✅ CORRECTIONS APPLIQUÉES

### 1. ✅ esp32-compat.php - CORRIGÉ

**Fichier modifié:** `ffp3/ffp3datas/public/esp32-compat.php`

**Changements appliqués:**

```php
// Lignes 15-18 - AJOUTÉ
$protocol = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off') ? 'https' : 'http';
$host = $_SERVER['HTTP_HOST'] ?? 'iot.olution.info';
$fullBaseUrl = $protocol . '://' . $host . $basePath;

// Ligne 37 - CORRIGÉ (action output_update)
$targetUrl = $fullBaseUrl . '/api/outputs/' . urlencode($id) . '/state';
$ch = curl_init($targetUrl);
// + Ajout timeout et gestion d'erreur (lignes 45-56)

// Ligne 70 - CORRIGÉ (action output_delete)
$targetUrl = $fullBaseUrl . '/api/outputs/' . urlencode($id);
$ch = curl_init($targetUrl);
// + Ajout timeout et gestion d'erreur (lignes 78-85)
```

**Résultat:** Les URLs curl sont maintenant complètes et fonctionnelles.

### 2. ✅ BasePath - AMÉLIORÉ

**Fichier modifié:** `ffp3/ffp3datas/public/index.php`

**Changements appliqués:**
- Ajout de commentaires explicatifs (lignes 25-26)
- Ajout d'une ligne de debug optionnelle (ligne 33)

**Résultat:** Code plus clair et débogable.

### 3. ⚠️ post-data.php - À CLARIFIER

**Action à décider:**
- Le fichier `public/post-data.php` semble être un doublon standalone
- Il n'utilise PAS le routing Slim mais implémente sa propre logique
- **Question pour l'utilisateur:** Est-ce un fichier legacy ou intentionnel ?

**Recommandation:**
- Si legacy → Peut être supprimé
- Si intentionnel → Documenter son usage et pourquoi il coexiste avec PostDataController

---

## 🧪 Tests Recommandés

**Script de test automatique créé:** `ffp3/test_endpoints_esp32.php`

### Exécuter les tests automatiques:

```bash
cd ffp3
php test_endpoints_esp32.php
```

Ce script teste automatiquement:
1. GET /api/outputs/states/1 (PROD)
2. GET /api/outputs-test/states/1 (TEST)
3. POST /post-data (sans API key pour tester l'erreur)
4. Endpoint legacy esp32-compat.php
5. Routing Slim général (dashboard)

### Tests manuels (curl):

#### Test 1: Endpoint POST-DATA
```bash
curl -X POST "http://iot.olution.info/ffp3/ffp3datas/post-data" \
  -d "api_key=VOTRE_CLE&sensor=ESP32_1&version=10.52&TempAir=22.5&Humidite=65.0&TempEau=18.3" \
  -d "EauPotager=50&EauAquarium=45&EauReserve=80&diffMaree=2&Luminosite=150" \
  -d "etatPompeAqua=0&etatPompeTank=0&etatHeat=1&etatUV=0" \
  -d "bouffeMatin=8&bouffeMidi=12&bouffeSoir=18&resetMode=0"
```

**Résultat attendu:** `Données enregistrées avec succès`

#### Test 2: Endpoint OUTPUT-STATE
```bash
curl "http://iot.olution.info/ffp3/ffp3datas/api/outputs/states/1"
```

**Résultat attendu:** `{"2":"1","3":"0","4":"1",...}`

#### Test 3: esp32-compat.php (après correction)
```bash
# Test redirection outputs_state
curl "http://iot.olution.info/ffp3/ffp3datas/public/esp32-compat.php?action=outputs_state&board=1" -L

# Test output_update
curl "http://iot.olution.info/ffp3/ffp3datas/public/esp32-compat.php?action=output_update&id=5&state=1"
```

---

## 📝 Résumé

| Élément | État | Action |
|---------|------|--------|
| POST-DATA endpoint | ✅ OK | Aucune |
| OUTPUT-STATE endpoint | ✅ OK | Aucune |
| PostDataController | ✅ OK | Aucune |
| OutputController | ✅ OK | Aucune |
| esp32-compat.php | ✅ **CORRIGÉ** | Tester les corrections |
| BasePath calculation | ✅ **AMÉLIORÉ** | Tester avec debug si besoin |
| post-data.php standalone | 🟡 À clarifier | Décider si legacy ou intentionnel |

---

## 🎯 Actions Restantes

1. ✅ **FAIT:** Corrections `esp32-compat.php`
2. ✅ **FAIT:** Amélioration commentaires basePath
3. 🔄 **À FAIRE:** Exécuter `php ffp3/test_endpoints_esp32.php` pour valider
4. 🔄 **À FAIRE:** Tester avec un vrai ESP32
5. 🟡 **À DÉCIDER:** Que faire avec `public/post-data.php` (garder ou supprimer)

---

## 📚 Références

- Configuration ESP32: `include/project_config.h` lignes 52-77
- Routes serveur: `ffp3/ffp3datas/public/index.php`
- Controller POST: `ffp3/ffp3datas/src/Controller/PostDataController.php`
- Controller OUTPUT: `ffp3/ffp3datas/src/Controller/OutputController.php`
- Documentation migration: `ffp3/ffp3datas/ESP32_MIGRATION.md`


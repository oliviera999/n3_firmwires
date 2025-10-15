# 🌐 Endpoints ESP32 ↔ Serveur - Configuration Complète

**Version ESP32**: 11.35  
**Version Serveur**: 11.36  
**Date**: 14 Octobre 2025  

---

## 📍 Endpoints Utilisés par ESP32

### Environnement Actif: **TEST** (`wroom-test`)

**Configuration**: `platformio.ini` ligne 90
```ini
[env:wroom-test]
build_flags = 
    -DPROFILE_TEST  ← Environnement TEST actif
```

**Endpoints** (`include/project_config.h` lignes 60-68):

#### 1️⃣ POST Data (Envoi données capteurs + états)
```cpp
POST_DATA_ENDPOINT = "/ffp3/post-data-test"
```

**URL Complète**:
```
http://iot.olution.info/ffp3/post-data-test
```

**Fichier serveur** (ANCIEN Legacy):
```
/path/to/ffp3/post-data-test.php  ← Fichier que tu m'as montré
```

**Fichier serveur** (NOUVEAU Moderne - **PAS UTILISÉ actuellement**):
```
/path/to/ffp3/public/post-data.php  ← Nos modifications v11.36
```

#### 2️⃣ GET Outputs State (Récupération états distants)
```cpp
OUTPUT_ENDPOINT = "/ffp3/api/outputs-test/state"
```

**URL Complète**:
```
http://iot.olution.info/ffp3/api/outputs-test/state
```

**Fichier serveur**:
```
/path/to/ffp3/public/index.php  ← Route Slim Framework
  └─> OutputController::getOutputsState()
```

---

## 🔄 Comparaison Environnements

| Aspect | TEST (wroom-test) | PROD (wroom-prod) |
|--------|-------------------|-------------------|
| **Profil** | `PROFILE_TEST` | `PROFILE_PROD` |
| **Endpoint POST** | `/ffp3/post-data-test` | `/ffp3/post-data` |
| **Endpoint GET** | `/ffp3/api/outputs-test/state` | `/ffp3/api/outputs/state` |
| **Table Data** | `ffp3Data2` | `ffp3Data` |
| **Table Outputs** | `ffp3Outputs2` | `ffp3Outputs` |

---

## 🚨 **PROBLÈME ACTUEL IDENTIFIÉ**

### Décalage Fichiers

**ESP32 appelle** :
```
POST → http://iot.olution.info/ffp3/post-data-test
```

**Fichier serveur actif** (probablement):
```
/path/to/ffp3/post-data-test.php  ← Ancien fichier legacy
```

**Nos modifications sont dans**:
```
/path/to/ffp3/public/post-data.php  ← Nouveau fichier moderne
```

**Résultat** : 🔴 **Nos modifications ne sont PAS utilisées !**

---

## ✅ Solutions

### Solution 1: **Modifier l'Ancien Fichier Legacy** ⭐ RECOMMANDÉ

**Fichier à modifier**: `/path/to/ffp3/post-data-test.php` (l'ancien que tu m'as montré)

Ajouter juste après la ligne 74 (après les derniers UPDATE) :

```php
// RIEN À CHANGER ! L'ancien fichier fait DÉJÀ tout correctement :

UPDATE ffp3Outputs2 SET state = '" . $etatHeat . "' WHERE gpio= '2';          ✓
UPDATE ffp3Outputs2 SET state = '" . $etatUV . "' WHERE gpio= '15';           ✓
UPDATE ffp3Outputs2 SET state = '" . $etatPompeAqua . "' WHERE gpio= '16';    ✓
UPDATE ffp3Outputs2 SET state = '" . $etatPompeTank . "' WHERE gpio= '18';    ✓
UPDATE ffp3Outputs2 SET state = '" . $bouffePetits . "' WHERE gpio= '108';    ✓
UPDATE ffp3Outputs2 SET state = '" . $bouffeGros . "' WHERE gpio= '109';      ✓
UPDATE ffp3Outputs2 SET state = '" . $resetMode . "' WHERE gpio= '110';       ✓
UPDATE ffp3Outputs2 SET state = '" . $mail . "' WHERE gpio= '100';            ✓
UPDATE ffp3Outputs2 SET state = '" . $mailNotif . "' WHERE gpio= '101';       ✓
UPDATE ffp3Outputs2 SET state = '" . $aqThreshold . "' WHERE gpio= '102';     ✓
UPDATE ffp3Outputs2 SET state = '" . $tankThreshold . "' WHERE gpio= '103';   ✓
UPDATE ffp3Outputs2 SET state = '" . $chauffageThreshold . "' WHERE gpio= '104'; ✓
UPDATE ffp3Outputs2 SET state = '" . $bouffeMat . "' WHERE gpio= '105';       ✓
UPDATE ffp3Outputs2 SET state = '" . $bouffeMidi . "' WHERE gpio= '106';      ✓
UPDATE ffp3Outputs2 SET state = '" . $bouffeSoir . "' WHERE gpio= '107';      ✓
UPDATE ffp3Outputs2 SET state = '" . $tempsGros . "' WHERE gpio= '111';       ✓
UPDATE ffp3Outputs2 SET state = '" . $tempsPetits . "' WHERE gpio= '112';     ✓
UPDATE ffp3Outputs2 SET state = '" . $tempsRemplissageSec . "' WHERE gpio= '113'; ✓
UPDATE ffp3Outputs2 SET state = '" . $limFlood . "' WHERE gpio= '114';        ✓
UPDATE ffp3Outputs2 SET state = '" . $WakeUp . "' WHERE gpio= '115';          ✓
UPDATE ffp3Outputs2 SET state = '" . $FreqWakeUp . "' WHERE gpio= '116';      ✓

// ✅ DÉJÀ COMPLET ! 17 GPIO mis à jour
```

**Verdict** : ✅ **L'ancien fichier legacy fait DÉJÀ tout ce qu'il faut !**

### Solution 2: Vérifier que le Fichier Legacy est Bien sur le Serveur

Le fichier `post-data-test.php` doit être accessible à :
```
http://iot.olution.info/ffp3/post-data-test
```

---

## 🔍 Diagnostic Erreur HTTP 500

Si l'ancien fichier legacy est bien en place et fait les UPDATE, l'erreur 500 vient probablement de :

### Possibilité 1: **Mauvais Chemin Fichier**
```bash
# Vérifier sur serveur:
ls -la /path/to/ffp3/post-data-test.php

# Si absent, créer/copier le fichier
```

### Possibilité 2: **Erreur SQL dans Multi-Query**
```php
// Le fichier legacy utilise multi_query():
if ($conn->multi_query($sql) === TRUE) {
    echo "New record created successfully";
}

// ⚠️ multi_query() peut échouer si:
// - Une des UPDATE échoue
// - GPIO n'existe pas dans ffp3Outputs2
// - Problème de permissions BDD
```

### Possibilité 3: **GPIO Manquants dans Table Outputs**

Vérifier que **tous les GPIO existent** dans `ffp3Outputs2` :

```sql
SELECT gpio, name, state 
FROM ffp3Outputs2 
WHERE gpio IN (2, 15, 16, 18, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116)
ORDER BY gpio;

-- Doit retourner 21 lignes
-- Si lignes manquantes, exécuter: ffp3/migrations/INIT_GPIO_BASE_ROWS.sql
```

---

## 📊 Résumé Endpoints

### ESP32 → Serveur (POST)

**Environnement TEST** (wroom-test actuel):
```
URL: http://iot.olution.info/ffp3/post-data-test
Fichier: /path/to/ffp3/post-data-test.php (legacy)
Méthode: POST
Content-Type: application/x-www-form-urlencoded

Payload (31 paramètres):
api_key=fdGTMoptd5CD2ert3
&sensor=esp32-wroom
&version=11.35
&TempAir=28.0
&Humidite=60.0
&TempEau=28.0
&EauPotager=209
&EauAquarium=209
&EauReserve=209
&diffMaree=0
&Luminosite=813
&etatPompeAqua=0
&etatPompeTank=0
&etatHeat=0          ← État chauffage
&etatUV=1
&bouffeMatin=8
&bouffeMidi=12
&bouffeSoir=19
&tempsGros=2
&tempsPetits=2
&aqThreshold=18
&tankThreshold=80
&chauffageThreshold=18
&mail=oliv.arn.lau@gmail.com
&mailNotif=checked
&resetMode=0
&tempsRemplissageSec=5
&limFlood=8
&WakeUp=0
&FreqWakeUp=6
&bouffePetits=0
&bouffeGros=0

Actions serveur:
1. INSERT INTO ffp3Data2 (25 colonnes)
2. UPDATE ffp3Outputs2 (17 GPIO) ← CRITIQUE pour chauffage
```

### Serveur → ESP32 (GET)

**Environnement TEST** (wroom-test actuel):
```
URL: http://iot.olution.info/ffp3/api/outputs-test/state
Fichier: /path/to/ffp3/public/index.php
Route: Slim Framework → OutputController::getOutputsState()
Méthode: GET

Réponse JSON (17 paramètres):
{
  "16": "0",           // pump_aqua
  "pump_aqua": "0",
  "18": 0,             // pump_tank
  "pump_tank": 0,
  "2": "0",            // heat ← État chauffage lu
  "heat": "0",
  "15": "1",           // light
  "light": "1",
  "101": "1",          // mailNotif
  "mailNotif": "1",
  "115": "0",          // WakeUp
  "WakeUp": "0",
  "108": "1",          // bouffePetits
  "109": "1",          // bouffeGros
  "110": "0",          // resetMode
  "100": "oliv.arn.lau@gmail.com",  // mail
  "mail": "oliv.arn.lau@gmail.com",
  "102": "18",         // aqThr
  "aqThr": "18",
  "103": "80",         // taThr
  "taThr": "80",
  "104": "18",         // chauff
  "chauff": "18",
  "105": "8",          // bouffeMat
  "bouffeMat": "8",
  "106": "12",         // bouffeMid
  "bouffeMid": "12",
  "107": "19",         // bouffeSoir
  "bouffeSoir": "19",
  "111": "2",          // tempsGros
  "tempsGros": "2",
  "112": "2",          // tempsPetits
  "tempsPetits": "2",
  "113": "5",          // tempsRemplissageSec
  "tempsRemplissageSec": "5",
  "114": "8",          // limFlood
  "limFlood": "8",
  "116": "6",          // FreqWakeUp
  "FreqWakeUp": "6"
}

Source: SELECT gpio, state FROM ffp3Outputs2
```

---

## 🎯 Conclusion

### **L'ancien fichier legacy fait DÉJÀ tout correctement !**

✅ Il met à jour **TOUS les GPIO** nécessaires (17)  
✅ Le chauffage **DEVRAIT** rester allumé

### **Donc pourquoi HTTP 500 ?**

Possibilités :
1. ❌ Fichier `post-data-test.php` absent ou inaccessible
2. ❌ Erreur SQL (GPIO manquant dans ffp3Outputs2)
3. ❌ Problème permissions MySQL
4. ❌ Erreur PHP (syntax, variables undefined)

---

## 🔧 Action Immédiate

**Vérifier les logs serveur PHP** pour voir l'erreur exacte :

```bash
ssh user@iot.olution.info
tail -f /var/log/apache2/error.log
# OU
tail -f /path/to/ffp3/error_log
```

Ou créer un fichier de test pour diagnostiquer :
```php
// test-post.php
<?php
error_reporting(E_ALL);
ini_set('display_errors', 1);

// Tester connexion BDD
$conn = new mysqli("localhost", "oliviera_iot", "Iot#Olution1", "oliviera_iot");
if ($conn->connect_error) {
    die("Connection failed: " . $conn->connect_error);
}
echo "BDD OK\n";

// Tester table existe
$result = $conn->query("SHOW TABLES LIKE 'ffp3Data2'");
echo "Table ffp3Data2: " . ($result->num_rows > 0 ? "EXISTS" : "NOT FOUND") . "\n";

// Tester GPIO existe
$result = $conn->query("SELECT COUNT(*) as c FROM ffp3Outputs2 WHERE gpio IN (2,15,16,18,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116)");
$row = $result->fetch_assoc();
echo "GPIO count: " . $row['c'] . " (attendu: 21)\n";
?>
```

Veux-tu que je crée un script de diagnostic complet pour identifier l'erreur exacte ?

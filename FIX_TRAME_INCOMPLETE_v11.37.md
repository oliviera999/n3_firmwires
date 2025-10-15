# ✅ Correctif Trame Incomplète - v11.37

**Date**: 14 Octobre 2025  
**Version ESP32**: 11.37  
**Version Serveur**: 11.37  
**Problème résolu**: Écrasement des états GPIO et données à 0 avec trame incomplète  

---

## 🚨 Problème Initial

Quand l'ESP32 envoyait une trame POST incomplète (données manquantes) :

### **INSERT ffp3Data**
```php
// AVANT v11.37
$TempAir = $_POST['TempAir'] ?? 0;     // Si absent → 0
$etatHeat = $_POST['etatHeat'] ?? 0;   // Si absent → 0

INSERT INTO ffp3Data2 VALUES (..., 0, 0, ...)
```
**Résultat** : ❌ Valeurs aberrantes dans historique (0°C, 0%, etc.)

### **UPDATE ffp3Outputs**
```php
// AVANT v11.37
$outputsToUpdate = [
    2 => $data->etatHeat,  // 0 si absent
];

UPDATE ffp3Outputs2 SET state = 0 WHERE gpio = 2;
```
**Résultat** : 🔥 Chauffage éteint alors qu'il devrait rester allumé !

---

## ✅ Solutions Appliquées

### 1️⃣ **NULL au lieu de 0 pour valeurs manquantes** (INSERT ffp3Data)

#### **post-data.php (Production)**

**AVANT** :
```php
tempAir: (float)$sanitize('TempAir'),  // (float)null = 0.0
```

**APRÈS** :
```php
// v11.37: Valeurs NULL si absentes (pas de cast 0)
tempAir: $sanitize('TempAir') !== null ? (float)$sanitize('TempAir') : null,
```

**Résultat** :
```sql
-- Si TempAir absent dans POST
INSERT INTO ffp3Data2 (TempAir, ...) VALUES (NULL, ...)
                                             ^^^^
-- Au lieu de 0.0
```

#### **post-data-test.php (Test - Legacy)**

**AVANT** :
```php
$TempAir = $_POST['TempAir'] ?? 0;
INSERT ... VALUES ('$TempAir', ...)  // '0' si absent
```

**APRÈS** :
```php
$TempAir = $_POST['TempAir'] ?? null;
$sqlValue = function($val) {
    return ($val === null) ? 'NULL' : "'$val'";
};
INSERT ... VALUES (" . $sqlValue($TempAir) . ", ...)  // NULL si absent
```

---

### 2️⃣ **UPDATE seulement GPIO présents** (ffp3Outputs)

#### **post-data.php (Production)**

**AVANT** :
```php
$outputsToUpdate = [
    2 => $data->etatHeat,  // 0 si absent
];

foreach ($outputsToUpdate as $gpio => $state) {
    $outputRepo->updateState($gpio, (int)$state);  // UPDATE avec 0
}
```

**APRÈS** :
```php
// v11.37: Seulement les GPIO PRÉSENTS dans POST
$outputsToUpdate = [
    2 => isset($_POST['etatHeat']) ? $data->etatHeat : null,
    //   ^^^^^^^^^^^^^^^^^^^^^^^ Vérification explicite
];

foreach ($outputsToUpdate as $gpio => $state) {
    if ($state !== null && $state !== '') {  // Seulement si présent
        $outputRepo->updateState($gpio, (int)$state);
    }
}
```

#### **post-data-test.php (Test - Legacy)**

**AVANT** :
```php
// UPDATE SYSTÉMATIQUE (même si absent)
$sql .= "UPDATE ffp3Outputs2 SET state = '$etatHeat' WHERE gpio= '2';";
```

**APRÈS** :
```php
// UPDATE seulement si présent dans POST (v11.37)
if (isset($_POST['etatHeat'])) {
    $sql .= "UPDATE ffp3Outputs2 SET state = '$etatHeat' WHERE gpio= '2';";
}
```

---

## 📊 Comparaison Avant/Après

### Scénario : Trame sans `etatHeat`

| Étape | AVANT v11.37 | APRÈS v11.37 |
|-------|--------------|--------------|
| POST sans `etatHeat` | `$etatHeat = 0` | `$etatHeat = null` |
| INSERT ffp3Data | `etatHeat = 0` | `etatHeat = NULL` |
| Condition UPDATE | `if ($state !== null)` → TRUE | `if ($state !== null)` → FALSE |
| UPDATE ffp3Outputs | GPIO 2 = 0 (éteint) | ❌ Pas d'UPDATE |
| État GPIO 2 | `state = 0` | `state = 1` (inchangé) |
| Chauffage | ❌ S'éteint | ✅ Reste allumé |

---

## 📁 Fichiers Modifiés

### 1. **post-data.php** (Production)

**Fichier** : `ffp3/public/post-data.php`

**Modifications** :
- **Lignes 125-190** : NULL au lieu de cast 0 pour toutes les valeurs
- **Lignes 208-234** : isset() avant UPDATE GPIO

**Changements** :
```diff
- tempAir: (float)$sanitize('TempAir'),
+ tempAir: $sanitize('TempAir') !== null ? (float)$sanitize('TempAir') : null,

- 2 => $data->etatHeat,
+ 2 => isset($_POST['etatHeat']) ? $data->etatHeat : null,
```

---

### 2. **post-data-test-CORRECTED.php** (Test - Legacy)

**Fichier** : `ffp3/post-data-test-CORRECTED.php`

**Modifications** :
- **Lignes 18-50** : Variables = null au lieu de 0 par défaut
- **Lignes 68-106** : INSERT avec NULL au lieu de valeurs vides
- **Lignes 108-171** : UPDATE seulement si isset()

**Changements** :
```diff
- $TempAir = $_POST['TempAir'] ?? 0;
+ $TempAir = $_POST['TempAir'] ?? null;

- INSERT ... VALUES ('$TempAir', ...)
+ INSERT ... VALUES (" . $sqlValue($TempAir) . ", ...)  // NULL si absent

- $sql .= "UPDATE ... WHERE gpio= '2';";
+ if (isset($_POST['etatHeat'])) {
+     $sql .= "UPDATE ... WHERE gpio= '2';";
+ }
```

---

### 3. **project_config.h** (Version ESP32)

**Fichier** : `include/project_config.h`

**Modification** :
```diff
- constexpr const char* VERSION = "11.35";
+ constexpr const char* VERSION = "11.37";
```

---

## 🎯 Résultats

### **Trame Complète** (comportement inchangé)
- ✅ INSERT toutes valeurs
- ✅ UPDATE tous les GPIO
- ✅ Fonctionnement normal

### **Trame Incomplète** (comportement CORRIGÉ)

**Exemple** : POST sans `TempAir`, `etatHeat`, `bouffeMatin`

| Aspect | AVANT v11.37 | APRÈS v11.37 |
|--------|--------------|--------------|
| **ffp3Data2.TempAir** | `0.0` ❌ | `NULL` ✅ |
| **ffp3Data2.etatHeat** | `0` ❌ | `NULL` ✅ |
| **ffp3Outputs2 GPIO 2** | `state = 0` ❌ | Inchangé ✅ |
| **ffp3Outputs2 GPIO 105** | `state = 0` ❌ | Inchangé ✅ |
| **Chauffage** | S'éteint ❌ | Reste allumé ✅ |
| **Config nourrissage** | 00:00 ❌ | Préservée ✅ |

---

## ✅ Avantages

### 1. **Protection contre Écrasement**
- ✅ États GPIO préservés si données manquantes
- ✅ Chauffage ne s'éteint plus accidentellement
- ✅ Pompes ne s'arrêtent plus accidentellement
- ✅ Configurations non réinitialisées

### 2. **Historique Propre**
- ✅ Valeurs NULL au lieu de 0 aberrants
- ✅ Graphiques non faussés
- ✅ Statistiques précises
- ✅ Distinction claire entre 0 réel et donnée manquante

### 3. **Flexibilité**
- ✅ ESP32 peut envoyer trames partielles (mise à jour sélective)
- ✅ Meilleure gestion des erreurs réseau
- ✅ Économie bande passante

---

## 🧪 Tests Validation

### Test 1 : Trame Minimale
```powershell
curl.exe -X POST http://iot.olution.info/ffp3/post-data-test `
  -d "api_key=xxx" `
  -d "sensor=test-minimal" `
  -d "version=11.37"
```

**Résultat attendu** :
- ✅ INSERT avec sensor/version uniquement
- ✅ Autres colonnes = NULL
- ✅ Aucun UPDATE GPIO
- ✅ États GPIO inchangés

---

### Test 2 : Trame Partielle (sans chauffage)
```powershell
curl.exe -X POST http://iot.olution.info/ffp3/post-data-test `
  -d "api_key=xxx" `
  -d "sensor=test-partial" `
  -d "version=11.37" `
  -d "TempAir=25.0" `
  -d "etatUV=1"
```

**Résultat attendu** :
- ✅ INSERT avec TempAir = 25.0, etatUV = 1, reste NULL
- ✅ UPDATE GPIO 15 (UV) = 1
- ❌ Pas d'UPDATE GPIO 2 (chauffage) → reste à son état
- ✅ Chauffage préservé

---

### Test 3 : Vérifier NULL dans BDD
```sql
-- Après test 1
SELECT sensor, TempAir, etatHeat 
FROM ffp3Data2 
WHERE sensor = 'test-minimal';

-- Résultat attendu:
-- sensor: test-minimal
-- TempAir: NULL
-- etatHeat: NULL
```

---

## 🚀 Déploiement

### Étape 1 : Commit Local
```powershell
cd "C:\Users\olivi\Mon Drive\travail\##olution\##Projets\##prototypage\platformIO\Projects\ffp5cs"

# Projet principal
git add include/project_config.h
git commit -m "v11.37: Version bump"

# Sous-module ffp3
cd ffp3
git add public/post-data.php
mv post-data-test-CORRECTED.php post-data-test.php
git add post-data-test.php
git commit -m "v11.37: Fix trame incomplète - NULL + isset() protection"
git push origin main
```

---

### Étape 2 : Déployer Serveur
```bash
# Sur serveur distant
ssh user@iot.olution.info
cd /path/to/ffp3
git pull origin main
```

---

### Étape 3 : Flash ESP32
```powershell
cd "C:\Users\olivi\Mon Drive\travail\##olution\##Projets\##prototypage\platformIO\Projects\ffp5cs"
pio run -e wroom-test --target upload
```

---

### Étape 4 : Monitoring Validation
```powershell
# 90 secondes de monitoring
pio device monitor --baud 115200 --filter direct
```

**Vérifier** :
- ✅ POST → HTTP 200
- ✅ Queue vide
- ✅ Chauffage stable
- ✅ Pas d'écrasement GPIO

---

## 📝 Notes Importantes

### Compatibilité Ascendante
- ✅ **Trame complète** : Fonctionnement identique à v11.36
- ✅ **ESP32 ancien** : Compatible (envoie toujours trame complète)
- ✅ **BDD existante** : Accepte NULL (colonnes déjà nullable)

### Colonnes BDD
- ✅ Toutes les colonnes de `ffp3Data2` acceptent NULL (sauf id, sensor, version)
- ✅ Colonne `state` dans `ffp3Outputs2` est VARCHAR (accepte NULL)
- ✅ Pas de modification structure BDD nécessaire

### ESP32
- ✅ Si erreur capteur → peut envoyer trame sans cette valeur
- ✅ Économie RAM (payload plus court)
- ✅ Meilleure résilience réseau

---

## 📚 Documentation

- **Analyse problème** : `COMPORTEMENT_TRAME_INCOMPLETE.md`
- **Spécification correctif** : `CORRECTIF_TRAME_INCOMPLETE.md`
- **Endpoints** : `ENDPOINTS_ESP32_SERVEUR.md`
- **Script test** : `test_trame_incomplete.ps1`

---

## ✅ Checklist Validation

- [x] Code modifié (post-data.php)
- [x] Code modifié (post-data-test.php)
- [x] Version incrémentée (11.37)
- [x] Documentation créée
- [ ] Commit + Push ffp3
- [ ] Déploiement serveur
- [ ] Flash ESP32
- [ ] Test trame complète
- [ ] Test trame incomplète
- [ ] Vérification BDD (NULL présents)
- [ ] Monitoring 90s
- [ ] Validation stabilité

---

**Statut** : ✅ Code prêt - En attente déploiement  
**Priorité** : HAUTE (Protection critique contre écrasement)  
**Impact** : Majeur (Stabilité système + Données historiques)


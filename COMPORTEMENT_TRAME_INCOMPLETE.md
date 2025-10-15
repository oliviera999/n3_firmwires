# 📡 Comportement avec Trame Incomplète

**Date**: 14 Octobre 2025  
**Question**: Que se passe-t-il si l'ESP32 envoie une trame POST incomplète ?  

---

## 🔍 Analyse du Code

### 1️⃣ Réception POST (post-data.php lignes 115-189)

```php
// Fonction de sanitization
$sanitize = static fn(string $key) => isset($_POST[$key]) ? htmlspecialchars(trim($_POST[$key])) : null;

// Construction SensorData
$data = new SensorData(
    sensor: $sanitize('sensor'),           // Si absent → null
    version: $sanitize('version'),         // Si absent → null
    tempAir: (float)$sanitize('TempAir'),  // Si absent → (float)null = 0.0
    etatHeat: (int)$sanitize('etatHeat'),  // Si absent → (int)null = 0
    // ...
);
```

**Comportement PHP** :
- `isset($_POST['key'])` → `false` si clé absente
- `$sanitize('key')` retourne `null` si absent
- `(int)null` = `0`
- `(float)null` = `0.0`
- `(string)null` = `""` (chaîne vide)

---

## 📊 Impact par Type de Donnée

### A. **Champs Texte** (sensor, version, mail)

**Si absent** :
```php
sensor: $sanitize('sensor')  // null
```

**Dans ffp3Data** :
```sql
INSERT INTO ffp3Data2 (sensor, ...) VALUES (null, ...)
                                            ^^^^
```

**Résultat** :
- ✅ INSERT accepté (colonnes acceptent NULL)
- ⚠️ Ligne créée avec `sensor = NULL`

---

### B. **Champs Numériques Float** (TempAir, Humidite, TempEau)

**Si absent** :
```php
tempAir: (float)$sanitize('TempAir')  // (float)null = 0.0
```

**Dans ffp3Data** :
```sql
INSERT INTO ffp3Data2 (TempAir, ...) VALUES (0.0, ...)
                                             ^^^
```

**Résultat** :
- ✅ INSERT accepté
- ⚠️ Température enregistrée à `0.0°C` (valeur incorrecte !)
- ⚠️ **Impossible de distinguer 0°C réel d'une donnée manquante**

---

### C. **Champs Numériques Int** (etatHeat, etatPompeAqua, etc.)

**Si absent** :
```php
etatHeat: (int)$sanitize('etatHeat')  // (int)null = 0
```

**Dans ffp3Data** :
```sql
INSERT INTO ffp3Data2 (etatHeat, ...) VALUES (0, ...)
                                              ^
```

**Dans ffp3Outputs (UPDATE)** :
```php
$outputsToUpdate = [
    2 => $data->etatHeat,  // 0 si absent
];

foreach ($outputsToUpdate as $gpio => $state) {
    if ($state !== null && $state !== '') {  // 0 passe le test !
        $outputRepo->updateState($gpio, (int)$state);  // UPDATE avec 0
    }
}
```

**Résultat** :
- ✅ INSERT accepté avec `etatHeat = 0`
- ⚠️ **GPIO 2 (chauffage) UPDATE à `0` (OFF)**
- 🚨 **Le chauffage s'éteint même s'il était allumé !**

---

## 🚨 **Problèmes Critiques**

### Problème 1 : **Écrasement des États GPIO**

**Scénario** :
1. Chauffage allumé : GPIO 2 = `1` dans ffp3Outputs2
2. ESP32 envoie trame incomplète **sans `etatHeat`**
3. Code PHP : `etatHeat = (int)null = 0`
4. UPDATE : `ffp3Outputs2 SET state = 0 WHERE gpio = 2`
5. **Chauffage éteint alors qu'il devrait rester allumé !**

**Impact** :
- 🔥 Chauffage s'éteint automatiquement
- 💧 Pompes stoppées
- 💡 Lumière éteinte
- **Tous les actionneurs à 0 !**

---

### Problème 2 : **Données Historiques Incorrectes**

**Scénario** :
- Trame sans températures
- `TempAir = 0.0`, `Humidite = 0.0`, `TempEau = 0.0`
- Ligne insérée dans ffp3Data2 avec valeurs aberrantes

**Impact** :
- 📊 Graphiques faussés
- 📉 Moyennes/statistiques incorrectes
- ⚠️ Alertes déclenchées (température = 0°C)

---

### Problème 3 : **Configurations Réinitialisées**

**Scénario** :
- Trame sans `bouffeMatin`, `chauffageThreshold`, etc.
- Valeurs deviennent `0`
- UPDATE dans ffp3Outputs2

**Impact** :
- ⏰ Horaires nourrissage = 00:00
- 🌡️ Seuil chauffage = 0°C
- 💧 Seuil remplissage = 0cm
- **Configuration complètement réinitialisée !**

---

## ✅ Comportement Actuel (Résumé)

| Donnée Absente | Valeur PHP | INSERT ffp3Data | UPDATE ffp3Outputs | Impact |
|---------------|------------|-----------------|-------------------|--------|
| `sensor` | `null` | `NULL` | - | ⚠️ Ligne sans identifiant |
| `TempAir` | `0.0` | `0.0` | - | ⚠️ Fausse température |
| `etatHeat` | `0` | `0` | GPIO 2 = `0` | 🚨 Chauffage éteint |
| `etatPompeAqua` | `0` | `0` | GPIO 16 = `0` | 🚨 Pompe arrêtée |
| `bouffeMatin` | `0` | `0` | GPIO 105 = `0` | 🚨 Horaire = 00:00 |
| `chauffageThreshold` | `0` | `0` | GPIO 104 = `0` | 🚨 Seuil = 0°C |

**Conclusion** : ❌ **Trame incomplète = Écrasement complet des états et configs !**

---

## 🛡️ Solutions Possibles

### Solution 1 : **Ne PAS UPDATE si valeur = 0** (Partiel)

**Modification post-data.php** :
```php
foreach ($outputsToUpdate as $gpio => $state) {
    // NE PAS update si 0 (pourrait être une donnée manquante)
    if ($state !== null && $state !== '' && $state !== 0) {
        $outputRepo->updateState($gpio, (int)$state);
    }
}
```

**Problème** : ❌ Impossible d'éteindre volontairement (state = 0 légitime)

---

### Solution 2 : **Vérifier Présence Explicite dans POST** (Recommandé)

**Modification post-data.php** :
```php
$outputsToUpdate = [
    16 => isset($_POST['etatPompeAqua']) ? $data->etatPompeAqua : null,
    18 => isset($_POST['etatPompeTank']) ? $data->etatPompeTank : null,
    2  => isset($_POST['etatHeat']) ? $data->etatHeat : null,
    15 => isset($_POST['etatUV']) ? $data->etatUV : null,
    // ...
];

foreach ($outputsToUpdate as $gpio => $state) {
    if ($state !== null) {  // Seulement si présent dans POST
        $outputRepo->updateState($gpio, (int)$state);
    }
}
```

**Avantage** : ✅ UPDATE seulement les valeurs envoyées  
**Résultat** : Si `etatHeat` absent → GPIO 2 non modifié (reste à son état précédent)

---

### Solution 3 : **Valider Trame Complète** (Strict)

**Modification post-data.php** :
```php
// Liste champs obligatoires
$requiredFields = [
    'sensor', 'version', 'TempAir', 'Humidite', 'TempEau',
    'etatPompeAqua', 'etatPompeTank', 'etatHeat', 'etatUV',
    // ...
];

// Vérifier tous présents
$missingFields = [];
foreach ($requiredFields as $field) {
    if (!isset($_POST[$field])) {
        $missingFields[] = $field;
    }
}

if (count($missingFields) > 0) {
    http_response_code(400);
    echo "Trame incomplète - Champs manquants: " . implode(', ', $missingFields);
    exit;
}
```

**Avantage** : ✅ Refuse trame incomplète  
**Inconvénient** : ❌ ESP32 doit TOUJOURS envoyer trame complète

---

## 🎯 Recommandation

### **Solution 2 : Vérifier Présence Explicite** ⭐

**Raison** :
1. ✅ Ne modifie QUE les valeurs envoyées
2. ✅ Préserve les états existants si données manquantes
3. ✅ Compatible avec envoi partiel (mise à jour sélective)
4. ✅ Pas de réinitialisation accidentelle

**Implémentation** :
- Modifier `post-data.php` (production)
- Modifier `post-data-test.php` (test)
- Utiliser `isset($_POST['key'])` avant UPDATE

---

## 📝 Comportement POST-Correction

**Avec Solution 2 appliquée** :

| Scénario | Avant | Après |
|----------|-------|-------|
| Trame complète | ✅ Tout UPDATE | ✅ Tout UPDATE |
| Trame sans `etatHeat` | ❌ GPIO 2 → 0 (éteint) | ✅ GPIO 2 inchangé |
| Trame sans `TempAir` | ❌ TempAir = 0.0 | ⚠️ TempAir = 0.0 (INSERT) |
| Trame sans configs | ❌ Configs → 0 | ✅ Configs inchangées |

**Note** : L'INSERT dans ffp3Data mettra toujours 0/null si absent (historique peut être incomplet)

---

## 🔧 Test

Pour tester le comportement actuel :

```powershell
.\test_trame_incomplete.ps1
```

Puis vérifier dans la BDD :
```sql
-- Voir données insérées
SELECT * FROM ffp3Data2 WHERE sensor LIKE 'test-%' ORDER BY id DESC LIMIT 3;

-- Voir GPIO modifiés
SELECT gpio, name, state, requestTime 
FROM ffp3Outputs2 
WHERE gpio IN (2,15,16,18) 
ORDER BY gpio;
```

---

## 📊 Conclusion

**Actuellement** :
- ❌ Trame incomplète → Valeurs = 0
- ❌ UPDATE écrase tous les GPIO
- 🚨 **Risque : Chauffage/pompes/configs réinitialisés**

**Avec Solution 2** :
- ✅ Trame incomplète → UPDATE seulement valeurs présentes
- ✅ États préservés si données manquantes
- ✅ **Sécurité : Pas d'écrasement accidentel**

**Action recommandée** : Implémenter Solution 2 dans les 2 endpoints


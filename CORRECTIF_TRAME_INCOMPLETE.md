# 🔧 Correctif : Gestion Trame Incomplète

**Date**: 14 Octobre 2025  
**Problème**: Trame incomplète écrase les états GPIO à 0  
**Solution**: Vérifier présence explicite avec `isset()` avant UPDATE  

---

## 🚨 Problème Actuel

**Code actuel (post-data.php ligne 208)** :
```php
$outputsToUpdate = [
    16 => $data->etatPompeAqua,     // Si absent dans POST → 0
    18 => $data->etatPompeTank,     // Si absent dans POST → 0
    2  => $data->etatHeat,          // Si absent dans POST → 0
    // ...
];

foreach ($outputsToUpdate as $gpio => $state) {
    if ($state !== null && $state !== '') {
        $outputRepo->updateState($gpio, (int)$state);  // UPDATE avec 0
    }
}
```

**Résultat** :
- ❌ Si `etatHeat` absent → `$data->etatHeat = 0` → GPIO 2 UPDATE à 0
- 🚨 Chauffage s'éteint même s'il était allumé

---

## ✅ Solution : Vérifier isset($_POST)

**Code corrigé** :
```php
$outputsToUpdate = [
    // Seulement si présent dans POST, sinon null (pas d'UPDATE)
    16 => isset($_POST['etatPompeAqua']) ? $data->etatPompeAqua : null,
    18 => isset($_POST['etatPompeTank']) ? $data->etatPompeTank : null,
    2  => isset($_POST['etatHeat']) ? $data->etatHeat : null,
    15 => isset($_POST['etatUV']) ? $data->etatUV : null,
    
    100 => isset($_POST['mail']) ? $data->mail : null,
    101 => isset($_POST['mailNotif']) ? ($data->mailNotif === 'checked' ? 1 : 0) : null,
    102 => isset($_POST['aqThreshold']) ? $data->aqThreshold : null,
    103 => isset($_POST['tankThreshold']) ? $data->tankThreshold : null,
    104 => isset($_POST['chauffageThreshold']) ? $data->chauffageThreshold : null,
    105 => isset($_POST['bouffeMatin']) ? $data->bouffeMatin : null,
    106 => isset($_POST['bouffeMidi']) ? $data->bouffeMidi : null,
    107 => isset($_POST['bouffeSoir']) ? $data->bouffeSoir : null,
    108 => isset($_POST['bouffePetits']) ? $data->bouffePetits : null,
    109 => isset($_POST['bouffeGros']) ? $data->bouffeGros : null,
    110 => isset($_POST['resetMode']) ? $data->resetMode : null,
    111 => isset($_POST['tempsGros']) ? $data->tempsGros : null,
    112 => isset($_POST['tempsPetits']) ? $data->tempsPetits : null,
    113 => isset($_POST['tempsRemplissageSec']) ? $data->tempsRemplissageSec : null,
    114 => isset($_POST['limFlood']) ? $data->limFlood : null,
    115 => isset($_POST['WakeUp']) ? $data->wakeUp : null,
    116 => isset($_POST['FreqWakeUp']) ? $data->freqWakeUp : null,
];

foreach ($outputsToUpdate as $gpio => $state) {
    // Seulement si $state !== null (donc présent dans POST)
    if ($state !== null && $state !== '') {
        if ($gpio === 100) {
            $outputRepo->updateState($gpio, $state); // VARCHAR pour email
        } else {
            $outputRepo->updateState($gpio, (int)$state);
        }
        $updatedCount++;
    }
}
```

**Résultat** :
- ✅ Si `etatHeat` absent → `null` → Pas d'UPDATE → GPIO 2 inchangé
- ✅ Chauffage reste dans son état précédent

---

## 📝 Modifications à Appliquer

### 1. `post-data.php` (Production)

**Fichier** : `ffp3/public/post-data.php`  
**Lignes à modifier** : 208-233

---

### 2. `post-data-test.php` (Test - Legacy)

**Code actuel** :
```php
$sql .= "UPDATE ffp3Outputs2 SET state = '$etatHeat' WHERE gpio= '2';";
```

**Code corrigé** :
```php
// Seulement UPDATE si présent dans POST
if (isset($_POST['etatHeat'])) {
    $sql .= "UPDATE ffp3Outputs2 SET state = '" . $conn->real_escape_string($etatHeat) . "' WHERE gpio= '2';";
}
if (isset($_POST['etatUV'])) {
    $sql .= "UPDATE ffp3Outputs2 SET state = '" . $conn->real_escape_string($etatUV) . "' WHERE gpio= '15';";
}
if (isset($_POST['etatPompeAqua'])) {
    $sql .= "UPDATE ffp3Outputs2 SET state = '" . $conn->real_escape_string($etatPompeAqua) . "' WHERE gpio= '16';";
}
// ... pour tous les GPIO
```

---

## 🎯 Avantages

| Avant | Après |
|-------|-------|
| Trame incomplète → GPIO = 0 | Trame incomplète → GPIO inchangé |
| Chauffage s'éteint accidentellement | Chauffage reste à son état |
| Configs réinitialisées | Configs préservées |
| Tous GPIO UPDATE (21) | Seulement GPIO présents UPDATE |

---

## ⚠️ Limitation

**INSERT ffp3Data reste inchangé** :
- Si `TempAir` absent → INSERT avec `TempAir = 0.0`
- Historique peut contenir valeurs aberrantes

**Solution complémentaire** :
- Utiliser `NULL` au lieu de `0` pour valeurs manquantes
- Modifier SensorRepository pour accepter NULL

---

## 🚀 Application

Veux-tu que j'applique ce correctif maintenant ?

Cela modifiera :
1. ✅ `ffp3/public/post-data.php` (production)
2. ✅ `ffp3/post-data-test-CORRECTED.php` (test)

Puis il faudra déployer sur le serveur.


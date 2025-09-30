# Fix OTA - Problème de Basculement entre Partitions

## Problème Identifié

Lors des mises à jour OTA successives, l'ESP32 redémarrait toujours sur l'ancienne partition au lieu de basculer vers la nouvelle partition mise à jour. Ce problème se manifestait ainsi :

1. **Première mise à jour OTA** : Fonctionne correctement
2. **Deuxième mise à jour OTA** : L'ESP32 redémarre sur l'ancienne version
3. **Flash manuel** : Même problème, retour à l'ancienne partition

## Cause du Problème

Le système OTA de l'ESP32 utilise deux partitions (app0 et app1) pour permettre les mises à jour sans risque :
- Une partition contient le firmware actuel
- L'autre partition reçoit le nouveau firmware

Le problème était que `Update.end()` ne marquait pas automatiquement la nouvelle partition comme partition de boot active. Sans cette étape cruciale, l'ESP32 continuait à démarrer depuis l'ancienne partition.

## Solution Appliquée

### 1. Marquage Explicite de la Partition de Boot

Après `Update.end()`, nous ajoutons maintenant :

```cpp
// Marquer explicitement la nouvelle partition comme boot partition
const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
if (update_partition) {
    esp_err_t err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        // Gestion d'erreur
    }
}
```

Cette modification a été appliquée dans les 3 méthodes de téléchargement :
- `downloadFirmwareModern()`
- `downloadFirmware()` (fallback)
- `downloadFirmwareUltraRevolutionary()`

### 2. Diagnostics Améliorés

#### Avant la mise à jour :
```
📊 État des partitions AVANT mise à jour:
  - Partition en cours: app0 (0x10000)
  - Partition de boot: app0 (0x10000)
  - Prochaine partition OTA: app1 (0x380000)
```

#### Après la mise à jour :
```
📊 État des partitions APRÈS mise à jour:
  - Partition en cours: app0 (0x10000)
  - Partition de boot (prochaine): app1 (0x380000)
  - Prochaine partition OTA: app0 (0x10000)
```

### 3. Validation au Démarrage

La fonction `validatePendingOta()` a été améliorée pour :
- Afficher l'état détaillé des partitions
- Détecter si on a changé de partition
- Valider automatiquement la nouvelle image
- Annuler le rollback automatique

## Comportement Attendu Après Fix

### Première mise à jour OTA :
1. Firmware téléchargé dans app1
2. app1 marquée comme partition de boot
3. Redémarrage → boot depuis app1
4. Validation de l'image au démarrage

### Deuxième mise à jour OTA :
1. Firmware téléchargé dans app0 (alternance)
2. app0 marquée comme partition de boot
3. Redémarrage → boot depuis app0
4. Validation de l'image au démarrage

## Test de Validation

Pour vérifier que le fix fonctionne :

1. **Noter la partition actuelle** dans les logs au démarrage
2. **Effectuer une mise à jour OTA**
3. **Vérifier après redémarrage** que la partition a changé
4. **Répéter l'opération** pour confirmer l'alternance

## Table de Partitions Utilisée

```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,   0x5000
otadata,  data, ota,     0xE000,   0x2000
app0,     app,  ota_0,   0x10000,  0x370000
app1,     app,  ota_1,   0x380000, 0x370000
spiffs,   data, spiffs,  0x6F0000, 0x110000
```

Les deux partitions app0 et app1 ont la même taille (0x370000 = 3.44 MB), permettant l'alternance complète.

## Logs de Diagnostic

Les nouveaux logs permettent de suivre précisément le processus :

```
[OTA] 🔄 Marquage de la nouvelle partition comme boot: app1
[OTA] ✅ Nouvelle partition marquée comme boot avec succès
[OTA] 📊 État des partitions APRÈS mise à jour:
[OTA]   - Partition de boot (prochaine): app1 (0x380000)
```

## Impact et Bénéfices

- ✅ **Mises à jour successives fonctionnelles** : Plus de blocage sur l'ancienne partition
- ✅ **Rollback automatique** : Si la nouvelle image échoue, retour automatique à l'ancienne
- ✅ **Traçabilité complète** : Logs détaillés du processus de basculement
- ✅ **Robustesse accrue** : Validation explicite à chaque étape

## Date de Correction

**12 Septembre 2025**

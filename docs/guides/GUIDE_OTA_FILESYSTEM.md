# Guide OTA Filesystem - Mise à jour des pages web via OTA

## Vue d'ensemble

Le système OTA a été étendu pour permettre la mise à jour des pages web stockées dans la partition LittleFS (spiffs) en plus du firmware. Cette fonctionnalité permet de mettre à jour l'interface utilisateur sans redéployer tout le firmware.

## Architecture

### Partitions utilisées
- **app0/app1** : Firmware (code exécutable)
- **spiffs** : Filesystem LittleFS (pages web, CSS, JS)

### Processus de mise à jour
1. Téléchargement et installation du nouveau firmware
2. Si disponible, téléchargement et installation du nouveau filesystem
3. Redémarrage pour appliquer les changements

## Configuration des métadonnées JSON

### Structure des métadonnées étendues

```json
{
  "version": "10.12",
  "bin_url": "https://server.com/ota/esp32-s3/firmware.bin",
  "size": 1678528,
  "md5": "a1b2c3d4e5f6789012345678901234567",
  "filesystem_url": "https://server.com/ota/esp32-s3/filesystem.bin",
  "filesystem_size": 524288,
  "filesystem_md5": "f6e5d4c3b2a1908765432109876543210",
  "channels": {
    "prod": {
      "esp32-s3": {
        "bin_url": "https://server.com/ota/esp32-s3/firmware.bin",
        "version": "10.12",
        "size": 1678528,
        "md5": "a1b2c3d4e5f6789012345678901234567",
        "filesystem_url": "https://server.com/ota/esp32-s3/filesystem.bin",
        "filesystem_size": 524288,
        "filesystem_md5": "f6e5d4c3b2a1908765432109876543210"
      }
    }
  }
}
```

### Champs requis pour le filesystem

- `filesystem_url` : URL du fichier filesystem.bin
- `filesystem_size` : Taille en bytes du filesystem
- `filesystem_md5` : Hash MD5 pour validation (optionnel)

## Utilisation

### 1. Préparation des fichiers

#### Génération du filesystem
```bash
# Utiliser mklittlefs pour créer le fichier filesystem.bin
mklittlefs -c data/ -s 1114112 -o filesystem.bin
```

#### Vérification du MD5
```bash
# Calculer le MD5 du filesystem
md5sum filesystem.bin
```

### 2. Déploiement sur le serveur

Placez les fichiers sur votre serveur web :
```
ota/
├── metadata.json
├── esp32-s3/
│   ├── firmware.bin
│   └── filesystem.bin
└── esp32-wroom/
    ├── firmware.bin
    └── filesystem.bin
```

### 3. Mise à jour automatique

Le système OTA vérifie automatiquement les métadonnées et télécharge :
- Le firmware si une nouvelle version est disponible
- Le filesystem si une nouvelle version est fournie

## Comportement du système

### Sélection des artefacts
1. **Firmware** : Toujours téléchargé si une nouvelle version est disponible
2. **Filesystem** : Téléchargé seulement si :
   - Une URL filesystem est fournie dans les métadonnées
   - Le firmware a été mis à jour avec succès

### Gestion des erreurs
- Si le firmware échoue, le filesystem n'est pas téléchargé
- Si le filesystem échoue, le firmware reste mis à jour
- Les erreurs sont loggées et notifiées par email

### Ordre d'exécution
1. Téléchargement du firmware (méthodes multiples avec fallbacks)
2. Installation du firmware
3. Téléchargement du filesystem (si disponible)
4. Installation du filesystem (écriture directe dans la partition spiffs)
5. Redémarrage

## Configuration technique

### Limites
- **Taille maximale filesystem** : 1,114,112 bytes (0x110000)
- **Partition cible** : spiffs (LittleFS)
- **Validation** : MD5 optionnel, taille obligatoire

### Sécurité
- Validation de la taille avant téléchargement
- Vérification que le filesystem tient dans la partition
- Effacement complet de la partition avant installation
- Support du mode `OTA_UNSAFE_FORCE` pour les tests

## Logs et notifications

### Messages de log
```
📁 Début du téléchargement du filesystem...
📁 Filesystem trouvé: 'https://server.com/filesystem.bin'
📁 Taille filesystem: 512.0 KB
🔐 MD5 filesystem: 'f6e5d4c3b2a1908765432109876543210'
✅ Mise à jour filesystem réussie !
```

### Notifications email
Les emails incluent maintenant :
- URL et taille du filesystem
- MD5 du filesystem
- Statut de la mise à jour filesystem

## Exemples d'utilisation

### Mise à jour firmware uniquement
```json
{
  "version": "10.12",
  "bin_url": "https://server.com/firmware.bin",
  "size": 1678528,
  "md5": "a1b2c3d4e5f6789012345678901234567"
}
```

### Mise à jour firmware + filesystem
```json
{
  "version": "10.12",
  "bin_url": "https://server.com/firmware.bin",
  "size": 1678528,
  "md5": "a1b2c3d4e5f6789012345678901234567",
  "filesystem_url": "https://server.com/filesystem.bin",
  "filesystem_size": 524288,
  "filesystem_md5": "f6e5d4c3b2a1908765432109876543210"
}
```

## Dépannage

### Problèmes courants

#### Filesystem trop grand
```
❌ Taille du filesystem trop importante: 2.0 MB > 1.1 MB
```
**Solution** : Réduire la taille du contenu ou optimiser les fichiers

#### Partition spiffs non trouvée
```
❌ Partition spiffs non trouvée
```
**Solution** : Vérifier la configuration des partitions dans `platformio.ini`

#### Échec d'écriture
```
❌ Erreur écriture filesystem: ESP_ERR_INVALID_SIZE
```
**Solution** : Vérifier l'espace libre et la connectivité réseau

## Avantages

✅ **Mise à jour sélective** : Firmware et filesystem indépendants
✅ **Efficacité** : Pas besoin de redéployer le firmware pour changer l'UI
✅ **Sécurité** : Validation MD5 et taille
✅ **Robustesse** : Gestion d'erreurs et fallbacks
✅ **Compatibilité** : Fonctionne avec l'infrastructure OTA existante

## Limitations

❌ **Redémarrage requis** : Le filesystem n'est pas rechargé dynamiquement
❌ **Taille limitée** : Contraint par la taille de la partition spiffs
❌ **Dépendance firmware** : Le filesystem n'est mis à jour que si le firmware réussit
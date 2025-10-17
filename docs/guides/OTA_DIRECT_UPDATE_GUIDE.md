# Guide de Mise à Jour OTA Directe

## 🚀 Procédure pour OTA sans Flash Manuel

### 1. Préparer le Serveur

Uploadez le binaire de firmware correspondant à votre modèle (ex: `firmware_s3_prod_v9.9.bin`) dans l'arborescence OTA.

### 2. Mettre à jour le fichier metadata.json sur le serveur

```json
{
  "version": "9.96",
  "bin_url": "https://exemple.com/ffp3/ota/esp32-s3/firmware.bin",
  "size": 1887483,
  "md5": "",
  "channels": {
    "prod": {
      "esp32-s3": {
        "version": "9.96",
        "bin_url": "https://exemple.com/ffp3/ota/esp32-s3/firmware.bin",
        "size": 1887483,
        "md5": ""
      },
      "esp32-wroom": {
        "version": "9.96",
        "bin_url": "https://exemple.com/ffp3/ota/esp32-wroom/firmware.bin",
        "size": 1765432,
        "md5": ""
      },
      "default": {
        "version": "9.96",
        "bin_url": "https://exemple.com/ffp3/ota/esp32-wroom/firmware.bin",
        "size": 1765432
      }
    },
    "test": {
      "esp32-s3": {
        "version": "9.96",
        "bin_url": "https://exemple.com/ffp3/ota/esp32-s3/firmware.bin",
        "size": 1887483
      }
    }
  }
}
```

### 3. Déclencher l'OTA

L'ESP32 vérifie automatiquement les mises à jour toutes les 2 heures (cf. `TimingConfig::OTA_CHECK_INTERVAL_MS`), ou vous pouvez :
- Redémarrer l'ESP32 pour forcer une vérification immédiate
- Attendre le prochain cycle de vérification

### 4. Surveiller les Logs

Connectez-vous au moniteur série pour voir le processus :

```bash
pio device monitor -b 115200
```

## ⚠️ Scénarios Possibles

### Scénario A : OTA Réussie ✅
- Le firmware est téléchargé et installé
- L'ESP32 redémarre sur la nouvelle partition
- Les OTA suivantes fonctionneront correctement

### Scénario B : OTA Téléchargée mais Pas Activée ⚠️

Si l'ESP32 redémarre toujours sur l'ancienne version après l'OTA :

1. **Vérifier dans les logs** quelle partition est active
2. **Utiliser le script de force** :
   ```bash
   python tools/force_ota_partition.py
   ```
3. **Redémarrer l'ESP32**

### Scénario C : Échec Total ❌

Si rien ne fonctionne, revenez au flash manuel :
1. Mettre l'ESP32 en mode bootloader (BOOT + RESET)
2. Flash manuel selon l'environnement (`s3-prod`, `s3-test`, `wroom-prod`, etc.) : `pio run -e s3-prod -t upload`

## 📊 Vérification du Succès

Après l'OTA, vous devriez voir dans les logs :

```
=== VALIDATION OTA AU DÉMARRAGE ===
[OTA] Partition en cours: app1 (0x380000)  // ← Changement de partition !
[OTA] Partition de boot: app1 (0x380000)
[OTA] État: IMAGE EN ATTENTE DE VALIDATION
[OTA] ✅ Image validée et rollback annulé
```

## 💡 Astuce Pro

Si vous avez accès SSH au serveur, vous pouvez surveiller les requêtes HTTP :
```bash
tail -f /var/log/apache2/access.log | grep metadata.json
```

Cela vous montrera quand l'ESP32 vérifie les mises à jour.

## URL et dossiers OTA attendus par le firmware

- URL des métadonnées fixe: `http://iot.olution.info/ffp3/ota/metadata.json` (voir `OTAConfig::getMetadataUrl()`)
- Dossiers par modèle: `esp32-s3/`, `esp32-wroom/` sous `http://iot.olution.info/ffp3/ota/`
- Nom des fichiers par défaut: `firmware.bin`, `version.json`, `manifest.json`

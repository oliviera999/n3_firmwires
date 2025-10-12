<!-- a7b1f7e7-c934-4799-8534-8ed72f58d67f 5a973995-f553-4ece-9836-4cebd43455e4 -->
# Plan: Flash + Monitor 90s - Validation Obligatoire v11.05

## Objectif

Valider le firmware v11.05 (Phase 2 @ 100%) sur ESP32 réel avant déploiement production.

## Prérequis

- ESP32 connecté sur port COM (à déterminer)
- Firmware compilé (SUCCESS @ 82.3% flash, 19.5% RAM)
- Filesystem data/ prêt

## Étapes d'Exécution

### 1. Détection Port COM

Identifier le port COM de l'ESP32:

```bash
pio device list
```

Si port occupé ou introuvable, tenter:

- Débrancher/rebrancher ESP32
- Vérifier gestionnaire périphériques Windows
- Fermer autres outils (Arduino IDE, monitor série)

### 2. Flash Firmware

Compiler + uploader le firmware:

```bash
pio run -e wroom-prod -t upload
```

**Attendu**:

- Upload SUCCESS
- Firmware ~1.67MB
- Durée: ~30-60 secondes

**Si échec**:

- Port occupé → fermer autres applications
- Permissions → redémarrer terminal admin
- Hardware → vérifier connexion USB

### 3. Flash Filesystem (SPIFFS)

Uploader les assets web (data/):

```bash
pio run -e wroom-prod -t uploadfs
```

**Attendu**:

- Upload SUCCESS
- Filesystem ~400KB
- Durée: ~20-40 secondes

### 4. Monitor 90 Secondes

Lancer le monitor série et capturer 90s:

```bash
pio device monitor --baud 115200 --filter esp32_exception_decoder
```

**Capturer dans fichier**:

```bash
pio device monitor --baud 115200 > monitor_v11.05_validation.log
# Attendre 90 secondes
# Ctrl+C pour arrêter
```

### 5. Analyse Logs

Analyser le fichier capturé en priorisant:

**🔴 PRIORITÉ 1 - CRITIQUE**:

- `Guru Meditation`
- `Panic`
- `Brownout`
- `Core dump`
- `Task watchdog`

**🟡 PRIORITÉ 2 - IMPORTANT**:

- `WiFi timeout`
- `WebSocket disconnect`
- `Heap low` / `malloc failed`
- `Stack overflow`

**🟢 PRIORITÉ 3 - INFO**:

- Utilisation mémoire (heap actuel/min)
- Connexions WiFi/WebSocket
- Activité capteurs (sauf DHT - ignorer)
- Cycles sleep/wake

**⚪ IGNORER**:

- Warnings DHT (connus et normaux)
- Deprecation ArduinoJson (futur)

### 6. Validation

**Critères de succès**:

- ✅ Aucun Panic/Guru Meditation
- ✅ WiFi stable (connecté)
- ✅ WebSocket fonctionnel
- ✅ Heap > 50KB minimum
- ✅ Aucun watchdog timeout
- ✅ Sleep/Wake cycles normaux

**Si problèmes détectés**:

- Documenter dans `MONITORING_V11.05_ISSUES.md`
- Corriger avant production
- Re-tester après correction

### 7. Rapport Final

Créer `VALIDATION_V11.05_FINALE.md`:

- Résumé monitoring 90s
- Points d'attention identifiés
- Validation production (OUI/NON)
- Actions correctives si nécessaire

## Estimation Durée

- Détection port: 2 min
- Flash firmware: 1 min
- Flash filesystem: 1 min
- Monitor 90s: 1.5 min
- Analyse logs: 5-10 min
- Rapport: 5 min

**Total**: ~15-20 minutes

## Risques

1. **Port COM occupé** → Fermer applications série
2. **ESP32 non détecté** → Vérifier connexion/driver
3. **Upload échoue** → Reset ESP32, retry
4. **Panic détecté** → Corriger avant prod
5. **Mémoire critique** → Optimiser davantage

## Livrables

- monitor_v11.05_validation.log (fichier brut)
- VALIDATION_V11.05_FINALE.md (analyse)
- Commit validation si tout OK
- GO/NO-GO production

## Notes Importantes

- Règle projet: **90s monitoring OBLIGATOIRE** après chaque flash
- Ignorer warnings DHT (normaux)
- Focus sur stabilité système
- Heap minimum acceptable: 50KB
- Production seulement si validation OK

---

**VALIDATION CRITIQUE AVANT PRODUCTION** ⚠️

### To-dos

- [ ] Détecter port COM ESP32
- [ ] Flash firmware v11.05 sur ESP32
- [ ] Flash filesystem (SPIFFS) sur ESP32
- [ ] Monitor 90 secondes et capturer logs
- [ ] Analyser logs (Panic, Guru, Watchdog, mémoire)
- [ ] Créer VALIDATION_V11.05_FINALE.md
- [ ] Commit + push si validation OK
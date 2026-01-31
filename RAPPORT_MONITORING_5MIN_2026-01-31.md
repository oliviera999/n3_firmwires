# Rapport de Monitoring 5 min - WROOM-TEST (31/01/2026)

**Contexte :** Erase flash complète → compile → flash firmware + LittleFS → monitoring 5 min → analyse multi-aspects  
**Log analysé :** `monitor_5min_2026-01-31_12-40-50.log`  
**Firmware :** v11.170 (post-correction web_client timeout)

---

## 1. Synthèse exécutive

| Critère | Statut | Remarque |
|---------|--------|----------|
| **Stabilité** | ✅ **STABLE** | Aucun crash (Guru Meditation, panic, reboot accidentel) |
| **Durée monitoring** | ✅ 4 min 59 s | Conforme à l’objectif 5 min |
| **Correctif HTTP** | ✅ Validé | Plus de LoadProhibited lors du timeout global |
| **WiFi** | ✅ OK | Connexion stable, 1 reconnexion au boot |
| **Mémoire** | ✅ OK | Pas d’alerte heap faible |

**Note :** Le rapport `generate_diagnostic_report` indique "2 crashes" — il s’agit de **faux positifs** : le pattern "panic" matche "diag_hasPanic NOT_FOUND" (clé NVS manquante au premier boot NVS vide). Aucun Guru Meditation ni rst:0xc dans le log.

---

## 2. Aspects monitorés – Détail

### 2.1 Boot et démarrage
- **Flash effacée** puis firmware + FS flashés correctement
- **NVS vide** au démarrage → 520 logs NVS NOT_FOUND (normal après erase)
- **Boot #1** (reboot #1 = compteur Diagnostics après premier boot)
- **Heap minimum** : 86 704 bytes (84.7 KB) au boot
- **Stacks HWM** : sensor 1344, web 7124, auto 7548, display 796, loop 2832 bytes

### 2.2 Réseau (WiFi)
- Connexion au réseau "inwi Home 4G 8306D9" (RSSI -60 dBm)
- 1 reconnexion au boot (STA not started → begin → OK)
- **107 timeouts** : principalement `NetworkClient readBytes` et `netRPC Timeout absolu (8000 ms)` au boot
- **1 netRPC timeout** au boot : `tryFetchConfigFromServer()` a dépassé 8 s (serveur lent/injoignable)

### 2.3 Serveur distant (POST/GET)
- **POST** : 5+ tentatives HTTP vers `iot.olution.info/ffp3/post-data-test` détectées
  - 12:41:11, 12:41:28, 12:41:33, 12:41:40, 12:44:04
  - Le script `diagnostic_serveur_distant` cherche `[PR] === DÉBUT POSTRAW ===` ; les logs utilisent un format différent → 0 détecté (faux négatif)
- **GET** : 0 tentative explicite (le fetch au boot a timeout avant réponse)
- **Heartbeat** : 0 (intervalle 5 min, monitoring 5 min → pas forcément atteint)

### 2.4 Mails
- **34 événements déclencheurs** (Chauffage OFF, OTA)
- **0 mail envoyé** : config email vide → fallback, mais la queue mail ne traite pas les envois
- Les mails "manquants" correspondent à des alertes non envoyées (email non configuré ou logique d’envoi désactivée)

### 2.5 Capteurs
- **DHT air** : non détecté (hardware de test sans capteur DHT)
- **Température eau** : capteur DS18B20 détecté et fonctionnel
- **Ultrasons** : quelques timeouts de lecture réactive (comportement attendu sans bassins)
- **47 erreurs capteurs** : cohérent avec l’absence de capteurs sur le banc de test

### 2.6 Mémoire et watchdog
- **Heap** : 33 vérifications, 0 alerte faible
- **Queue capteurs** : 0 erreur "queue pleine"
- **task_wdt** : aucune occurrence "task not found" dans ce log (correction ou contexte différent)
- **TWDT** : Watchdog désactivé pour OTA, Power gère le TWDT natif

### 2.7 Erreurs matérielles / PWM
- **ESP32PWM** : 2 erreurs "PWM channel failed to configure" (pins 12/13 – servomoteur)
- Comportement attendu si les broches sont utilisées autrement ou mal configurées

### 2.8 NVS
- **520 NOT_FOUND** : NVS vierge après erase → première initialisation
- Pas d’erreur d’écriture

---

## 3. Points positifs

1. **Aucun crash** : plus de LoadProhibited lié au timeout HTTP
2. **Correctif `web_client`** : vérification du timeout avant `begin()` semble efficace
3. **WiFi stable** après boot
4. **POST serveur** : requêtes HTTP envoyées (visibles dans les logs)
5. **Mémoire** : heap suffisant, pas d’alerte
6. **Queue** : pas de saturation

---

## 4. Points à surveiller

1. **netRPC timeout au boot** : fetch config serveur > 8 s → envisager timeout plus long ou fallback NVS plus rapide
2. **Mails** : 0 envoi malgré 34 événements → vérifier config email et logique d’envoi
3. **Patterns d’analyse** : `diagnostic_serveur_distant` et `analyze_log_exhaustive` ont des faux positifs/négatifs (pattern `[PR]`, mot-clé "panic")
4. **PWM** : erreurs sur pins 12/13 si servomoteur attendu

---

## 5. Fichiers générés

| Fichier | Description |
|---------|-------------|
| `monitor_5min_2026-01-31_12-40-50.log` | Log brut (16 092 lignes, 1.6 MB) |
| `monitor_5min_2026-01-31_12-40-50_analysis.txt` | Analyse `analyze_log.ps1` |
| `rapport_diagnostic_complet_2026-01-31_12-45-54.md` | Rapport complet |
| `diagnostic_serveur_distant_2026-01-31_12-45-54.txt` | Diagnostic serveur distant |
| `analyze_exhaustive_*.txt` | Analyse exhaustive |
| `check_emails_*.txt` | Analyse mails |

---

## 6. Conclusion

**Le système est stable sur cette session de 5 min.**  
Le correctif du timeout HTTP (vérification avant `begin()`) évite le crash LoadProhibited observé précédemment.  
Les "2 crashes" du rapport automatique sont des faux positifs.  
Prochaines étapes suggérées : monitoring plus long (1 h+), configuration email pour valider les mails, et tests avec capteurs connectés.

# Analyse des scripts PowerShell à la racine du projet FFP5CS

**Date:** 2026-01-31  
**Périmètre:** Tous les fichiers `.ps1` à la racine du projet (hors `scripts/`, `ffp3/`, `tools/`).

---

## 1. Résumé exécutif

| Catégorie | Nombre | Action recommandée |
|-----------|--------|--------------------|
| **À garder** | 22 | Conserver, éventuellement corriger les bugs mineurs |
| **À supprimer** | 3 | Scripts trompeurs ou redondants |
| **À fusionner ou simplifier** | 2 | Réduire la duplication |

---

## 2. Scripts à GARDER (et pourquoi)

### 2.1 Flash et monitoring

| Script | Rôle | Raison |
|--------|------|--------|
| **flash_and_monitor_10min_wroom_test.ps1** | Flash + monitoring 10 min + analyse intégrée | Script principal de validation 24/7, référencé dans les rapports et le workflow hardware. Indispensable. |
| **flash_and_monitor_until_reboot.ps1** | Flash + monitoring jusqu’à détection d’un reboot | Cas d’usage diagnostic de crash / stabilité. |
| **flash_wroom_test.ps1** | Flash seul (firmware + LittleFS) wroom-test | Flash rapide sans monitoring. Complément utile. |
| **flash_usb_coredump.ps1** | Flash prod avec partition coredump (erase + upload) | Cas spécifique production. À garder après correction (voir § 4). |
| **monitor_5min.ps1** | Monitoring 5 min avec option port + analyse | Version courte du monitoring, utile pour tests rapides. |
| **monitor_until_reboot.ps1** | Monitoring jusqu’au reboot (sans flash) | Utilisable quand le firmware est déjà flashé. Beaucoup de code commun avec flash_and_monitor_until_reboot mais cas d’usage distinct. |
| **monitor_log_wroom_test.ps1** | Monitoring avec enregistrement (durée paramétrable, défaut 3 min) | Alternative simple avec durée courte. |
| **wait_and_analyze_monitoring.ps1** | Attendre la fin d’un monitoring (fichier stable) puis analyser | Utile quand le monitoring est lancé dans un autre terminal. |

### 2.2 Diagnostic et analyse des logs

| Script | Rôle | Raison |
|--------|------|--------|
| **diagnostic_communication_serveur.ps1** | Diagnostic ciblé communication serveur (envoi/réception, [Sync]) | Utilisé dans le workflow diagnostic. |
| **diagnostic_serveur_distant.ps1** | Analyse détaillée POST/GET/Heartbeat, cohérence avec le code | Entrée du rapport de diagnostic complet. |
| **generate_diagnostic_report.ps1** | Agrège serveur + mails + analyse exhaustive → rapport Markdown | Point d’entrée pour un rapport unique. |
| **check_emails_from_log.ps1** | Vérification des mails (attendus vs envoyés/échoués) depuis un log | Utilisé par generate_diagnostic_report. |
| **analyze_log_exhaustive.ps1** | Analyse exhaustive (crashes, watchdog, mémoire, réseau, NVS, reboots) | Utilisé par generate_diagnostic_report. |
| **analyze_log.ps1** | Analyse générique (patterns critiques, POST, JSON, heap, DHT22) | Générique, à garder après correction du fichier par défaut (voir § 4). |
| **cleanup_old_logs.ps1** | Nettoyage logs vides, .errors orphelins, gros fichiers, anciens par type | Maintenance du repo. |

### 2.3 Vérification et utilitaires

| Script | Rôle | Raison |
|--------|------|--------|
| **check_monitoring_status.ps1** | Vérification périodique du monitoring (fichiers monitor_until_crash*, processus Python) | Surveillance d’une session en cours. |
| **check_monitoring.ps1** | État du monitoring (processus Python, dernier log, recherche de crash) | Vue rapide de l’état. |
| **install_cppcheck.ps1** | Installation de cppcheck (Chocolatey / winget / manuel) | Prérequis outil. |
| **sync_ffp3distant.ps1** | Sous-module ffp3distant (update / push / pull / status) | Gestion git. |
| **sync_all.ps1** | Mise à jour de tous les sous-modules + commit si changements | Gestion git. |
| **restart_esp32.ps1** | Reset ESP32 via esptool + lancement du monitor | Utile en dev (à améliorer : port en param, voir § 4). |
| **test_flash_and_monitor_script.ps1** | Test de cohérence du script flash_and_monitor_until_reboot (syntaxe, sections, paramètres) | Validation du script principal. |

### 2.4 Tests NVS / phase

| Script | Rôle | Raison |
|--------|------|--------|
| **test_nvs_optimization.ps1** | Test NVS Phase 1 (gestionnaire centralisé, cache, migration) | Validation des optimisations NVS. |
| **test_nvs_phase2.ps1** | Test NVS Phase 2 (compression JSON, flush différé) | Validation Phase 2. |
| **test_nvs_phase3.ps1** | Test NVS Phase 3 (rotation logs, nettoyage) | Validation Phase 3. |

---

## 3. Scripts à SUPPRIMER ou à réécrire

### 3.1 À supprimer (trompeurs ou inutiles)

| Script | Problème | Recommandation |
|--------|----------|----------------|
| **capture_debug_logs.ps1** | Ne capture pas réellement : lance un job qui n’écrit pas les logs NDJSON, puis affiche une commande manuelle à exécuter dans un autre terminal. Le script “attend” juste une durée. | **Supprimer** ou réécrire pour appeler vraiment une capture (ex. script Python ou pipeline clair). |
| **extract_debug_log.ps1** | N’extrait rien depuis LittleFS : se contente d’afficher des options (Serial, endpoint HTTP, etc.) et crée un fichier vide. | **Supprimer** ou réécrire quand une vraie méthode d’extraction sera implémentée. |
| **analyze_monitoring_10min.ps1** | Analyse très basique (quelques patterns), fichier par défaut hardcodé pour v11.77. Redondant avec **analyze_log.ps1** et **analyze_log_exhaustive.ps1**. | **Supprimer** et utiliser analyze_log.ps1 ou analyze_log_exhaustive.ps1 avec le dernier log. |

### 3.2 À fusionner ou simplifier

| Script | Problème | Recommandation |
|--------|----------|----------------|
| **flash_monitor_diagnose_simple.ps1** | Flash + **réinit NVS** + monitoring 5 min + rapport. Cas très spécifique (NVS reset). Utilise `--monitor-port` au lieu de `--port` pour pio (bug). | **Garder** seulement si le scénario “flash + effacer NVS + 5 min” est encore utilisé ; sinon supprimer. Dans tous les cas **corriger** `--monitor-port` → `--port`. |
| **test_phase3_simple.ps1** | Quasi doublon de **test_nvs_phase3.ps1** (même objectif Phase 3, port COM3 en dur, Get-WmiObject déprécié). | **Fusionner** avec test_nvs_phase3.ps1 (un seul script Phase 3 avec port paramétrable) ou supprimer test_phase3_simple.ps1. |

---

## 4. Erreurs de code identifiées

### 4.1 À corriger

| Fichier | Ligne / Contexte | Problème | Correction |
|---------|------------------|----------|------------|
| **flash_usb_coredump.ps1** | 12 | `$env = "wroom-prod"` : masque le drive PowerShell `$env` (variables d’environnement). | Renommer en `$envName` ou `$targetEnv` et utiliser cette variable dans `pio run -e $envName`. |
| **flash_monitor_diagnose_simple.ps1** | 113 | `--monitor-port` pour `pio run --target monitor` : option invalide. | Remplacer par `--port` (comme dans les autres scripts). |
| **analyze_log.ps1** | 3 | Fichier par défaut hardcodé : `monitor_15min_v11.156_2026-01-21_22-29-41.log`. | Utiliser le dernier fichier `monitor_*.log` (comme dans check_emails_from_log.ps1) si aucun paramètre n’est fourni. |
| **restart_esp32.ps1** | 10, 28 | Port COM6 en dur. | Ajouter un paramètre (ex. `-Port "COM6"`) et l’utiliser partout. |
| **flash_and_monitor_10min_wroom_test.ps1** | 84–85, 282, 361 | Commentaires / chaînes avec caractères mal encodés : `Ã‰TAPE`, `Ãªte`, `rÃ©sumÃ©`. | Réenregistrer le fichier en UTF-8 (BOM ou sans BOM selon préférence du projet). |
| **flash_and_monitor_10min_wroom_test.ps1** | 126 | `Write-Host "[OK] Compilation reussie"` est mal indenté (toujours exécuté, pas seulement en cas de succès). | Le placer à l’intérieur du bloc `if ($LASTEXITCODE -eq 0)` après la vérification (ou après le bloc if/else de compilation). |

### 4.2 Mineurs / bonnes pratiques

| Fichier | Contexte | Suggestion |
|---------|----------|------------|
| **test_phase3_simple.ps1**, **test_nvs_phase3.ps1** | `Get-WmiObject Win32_SerialPort` | Sur PowerShell Core, préférer `Get-CimInstance Win32_SerialPort`. |
| **wait_and_analyze_monitoring.ps1** | `(Get-Date).Second % 30 -eq 0` pour afficher toutes les 30 s | La condition peut afficher plusieurs fois dans la même minute. Préférer un compteur basé sur l’intervalle (ex. afficher toutes les 6 itérations de 5 s). |
| **diagnostic_communication_serveur.ps1** | 94 | `$firstLine.Substring(0, [Math]::Min(80, $firstLine.Length))` : si `$firstLine` est vide, `$firstLine.Length` peut poser problème selon le contexte. | Vérifier que `$firstLine` n’est pas vide avant d’appeler `Substring`. |

---

## 5. Synthèse des actions recommandées

1. **Supprimer** : `capture_debug_logs.ps1`, `extract_debug_log.ps1`, `analyze_monitoring_10min.ps1`.
2. **Corriger** :  
   - `flash_usb_coredump.ps1` ($env → $envName),  
   - `flash_monitor_diagnose_simple.ps1` (--monitor-port → --port),  
   - `analyze_log.ps1` (dernier log par défaut),  
   - `restart_esp32.ps1` (port en paramètre),  
   - `flash_and_monitor_10min_wroom_test.ps1` (encodage UTF-8 + indentation Write-Host compilation).
3. **Fusionner ou retirer** : `test_phase3_simple.ps1` avec `test_nvs_phase3.ps1` (un seul script Phase 3).
4. **Décider** : garder ou supprimer `flash_monitor_diagnose_simple.ps1` selon l’usage du scénario “flash + NVS reset + 5 min”.

---

*Analyse générée automatiquement. Vérifier les noms de fichiers et numéros de ligne après modifications.*

# Synthèse – Mails (ESP32 FFP5CS)

**Date :** 2026-02-03  
**Contexte :** Rôle, configuration, flux, limites et recommandations pour le sous-système mail du firmware.

---

## 1. Rôle

- **Alertes** : OTA (début / fin), mises à jour config (serveur distant, interface web), événements critiques.
- **Notifications** : Nourrissage manuel (petits / gros poissons), sleep / wake (raison, durée, résumé capteurs).
- **Actions manuelles** : Envoi d’email déclenché depuis l’interface web locale (ex. bouton nourrissage + email si configuré).

Les mails sont **optionnels** : le système fonctionne sans réseau et sans email (offline-first). En cas d’échec d’envoi, le mail peut être re-queued (retry limité) ou ignoré (queue pleine).

---

## 2. Configuration

| Élément | Détail |
|--------|--------|
| **Compilation** | `FEATURE_MAIL` (platformio.ini) — peut être désactivé pour économiser flash (WROOM). |
| **SMTP** | `EmailConfig::SMTP_HOST` (ex. smtp.gmail.com), `SMTP_PORT` 465 (SSL), identifiants dans `Secrets::`. |
| **Destinataire** | Variable distante `mail` (NVS / serveur) ou `EmailConfig::DEFAULT_RECIPIENT`. |
| **Activation** | Variable distante `mailNotif` (bool) — désactiver = pas d’envoi sauf cas particuliers (ex. OTA). |
| **Queue** | `TaskConfig::MAIL_QUEUE_SIZE` = 8 slots ; traitement **séquentiel** depuis `automationTask` (pas de tâche mail dédiée). |

---

## 3. Flux

- **send() / sendAlert()** : Enfilent un `MailQueueItem` dans la queue et retournent tout de suite. Si la queue n’est pas initialisée → envoi synchrone de secours.
- **processOneMailSync()** : Appelé depuis `automationTask` ; dépile au plus un mail par cycle, appelle `sendSync` ou `sendAlertSync`, et en cas d’échec remet une fois en queue (retry, max 2 tentatives).
- **sendSync()** : Vérifie réseau / heap, (re)connexion SMTP si besoin, construction du message (footer système, heure, etc.), envoi via ESP Mail Client, fermeture de session. Logs De/Objet/Sujet en **ASCII sûr** (`logSafeStr`) pour éviter caractères UTF-8 illisibles en série.

---

## 4. Limites et contraintes

| Contrainte | Valeur / comportement |
|------------|------------------------|
| **Timeout SMTP** | 5 s (`_smtp.setTCPTimeout(5)`), conforme règle projet. |
| **TLS** | Mutex partagé avec HTTPS (OTA) — `TLS_MUTEX_TIMEOUT_MS` 5 s. |
| **Heap** | Envoi évité si heap trop bas (seuils dans `config.h`). **Réserve 32 KB** : un bloc de 32 KB est réservé quand des mails sont en queue et que le heap le permet ; si le bloc max contigu devient < 32 KB (fragmentation), ce bloc est libéré juste avant l’envoi pour garantir 32 KB contigus (SMTP/TLS), puis ré-alloué après envoi. Stabilise l’envoi même avec heap fragmenté. |
| **Queue pleine** | Mail ignoré + log « Queue pleine, mail ignoré » (pas de blocage). |
| **Taille message** | `MailQueueItem::message` 512 octets ; sujet 96, toEmail 48. |
| **Réseau** | Pas d’envoi si WiFi non connecté ; attente stabilisation réseau limitée (ex. 3 s max) avant tentative SMTP. |

---

## 5. Cas d’usage principaux

- **Nourrissage** : `AutomatismSync` envoie un email si `_emailEnabled` et adresse configurée (petits / gros poissons).
- **OTA** : Début OTA → `sendAlert` ; fin (serveur distant ou interface web) → `sendAlert` avec destinataire selon `mailNotif` / `mail`.
- **Sleep / Wake** : `sendSleepMail` / `sendWakeMail` (raison, durée, résumé capteurs) vers `DEFAULT_RECIPIENT`.
- **Web** : Actions manuelles (ex. nourrissage) → réponse HTTP immédiate puis `sendManualActionEmail` (async ou sync si rate limit / heap faible).

---

## 6. Recommandations

1. **Surveillance** : En production, surveiller les logs `[Mail]` (succès / échec, queue pleine, reconnexion SMTP) et le heap autour des envois.
2. **Config** : Vérifier `mail` et `mailNotif` (NVS / variables distantes) et que SMTP (Secrets) est valide pour l’environnement.
3. **Analyse de logs** : Les scripts d’analyse (ex. rapport workflow) peuvent ne pas parser les lignes mail (encodage / format) ; en cas de problème, inspection manuelle des lignes `[Mail]` et `[MAIL_DBG]`.
4. **Pas de blocage** : Aucun envoi ne doit bloquer indéfiniment ; timeouts et queue bornée garantissent un retour rapide à l’automatisme.

---

*Synthèse établie à partir du code (mailer.cpp, config.h, automatism_sync, web_server, system_boot) et des rapports workflow 2026-02-03.*

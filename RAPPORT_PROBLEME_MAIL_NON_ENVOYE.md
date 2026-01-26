# Rapport - Problème: Aucun Mail Reçu ni Envoyé
## Date: 25 janvier 2026
## Analyse: Log de boot monitor_boot_2026-01-25_21-54-52.log

---

## 🔍 ADRESSES EMAIL IDENTIFIÉES

### Adresse d'ENVOI (Expéditeur)
- **Source:** `Secrets::AUTHOR_EMAIL`
- **Valeur:** `arnould.svt@gmail.com`
- **Statut:** ✅ Configurée correctement
- **Visible dans les logs:** Ligne 194 du log de boot

### Adresse de RÉCEPTION (Destinataire)
- **Source:** `g_appContext.automatism.getEmailAddress()` ou fallback
- **Valeur utilisée:** `oliv.arn.lau@gmail.com`
- **Statut:** ✅ Configurée (fallback utilisé)
- **Visible dans les logs:** Ligne 475 du log de boot
  ```
  [INFO] Cible: oliv.arn.lau@gmail.com
  ```

### Adresse par DÉFAUT (Fallback)
- **Source:** `EmailConfig::DEFAULT_RECIPIENT`
- **Valeur:** `oliv.arn.lau@gmail.com`
- **Utilisée quand:** `getEmailAddress()` retourne vide ou NULL

---

## ❌ PROBLÈME IDENTIFIÉ

### Ce qui s'est passé

1. **✅ Initialisation Mailer** (21:55:04.417)
   - SMTP configuré correctement
   - Queue mail créée (2 slots)

2. **✅ Mail ajouté à la queue** (21:55:13.150)
   - Sujet: "Démarrage système v11.156"
   - Destinataire: `oliv.arn.lau@gmail.com`
   - Message: "Mail de démarrage ENVOYÉ avec succès"
   - **⚠️ MAIS:** Ce message signifie seulement que le mail a été **ajouté à la queue**, pas qu'il a été **envoyé via SMTP**

3. **❌ TRAITEMENT DE LA QUEUE MANQUANT**
   - **Aucune trace** de `processOneMailSync()` dans le log
   - **Aucune trace** de connexion SMTP
   - **Aucune trace** d'envoi réel du mail

### Pourquoi le mail n'a pas été envoyé

Le système utilise un **traitement asynchrone** en deux étapes:

1. **Étape 1: Ajout à la queue** (FAIT ✅)
   - `sendAlert()` ajoute le mail à la queue FreeRTOS
   - Retourne immédiatement `true` (succès d'ajout)
   - **C'est ce qui génère le message "Mail ENVOYÉ avec succès"**

2. **Étape 2: Traitement de la queue** (MANQUANT ❌)
   - `automationTask` appelle `processOneMailSync()` périodiquement
   - Conditions requises:
     - `hasPendingMails()` doit retourner `true`
     - `TLSMutex::canConnect()` doit retourner `true`
   - **Cette étape n'a JAMAIS été exécutée dans le log de boot**

---

## 🔎 ANALYSE DÉTAILLÉE

### Code de traitement dans `app_tasks.cpp` (lignes 347-353)

```cpp
#if FEATURE_MAIL
if (g_ctx->mailer.hasPendingMails() && TLSMutex::canConnect()) {
  esp_task_wdt_reset();
  if (TLSMutex::acquire(3000)) {  // Timeout court pour ne pas bloquer
    g_ctx->mailer.processOneMailSync();  // Traite UN mail
    TLSMutex::release();
  }
}
#endif
```

### Pourquoi `processOneMailSync()` n'a pas été appelé

**Hypothèses possibles:**

1. **`hasPendingMails()` retourne `false`**
   - La queue pourrait être vide (problème de queue)
   - Ou la méthode ne détecte pas correctement les mails en attente

2. **`TLSMutex::canConnect()` retourne `false`**
   - Une autre opération TLS (HTTPS) est en cours
   - Le mutex TLS est verrouillé par `netTask` ou `webTask`
   - Dans le log, on voit plusieurs tentatives HTTPS qui échouent (erreur -10368: allocation mémoire)

3. **Le monitoring s'est arrêté trop tôt**
   - Le monitoring a duré 1 minute (jusqu'à 21:55:52)
   - Le mail a été ajouté à 21:55:13
   - Il y a peut-être eu un délai avant le traitement
   - **MAIS:** Aucune trace de traitement dans les 39 secondes restantes

4. **Problème de mémoire (erreur -10368)**
   - Dans le log, on voit: `[ssl_starttls_handshake():317]: (-10368) X509 - Allocation of memory failed`
   - Cette erreur apparaît lors des tentatives HTTPS
   - Si le heap est trop bas, `TLSMutex::canConnect()` pourrait retourner `false`
   - Le traitement mail nécessite aussi de la mémoire pour TLS

---

## 📊 SÉQUENCE TEMPORELLE DANS LE LOG

| Temps | Événement | Statut |
|-------|-----------|--------|
| 21:55:04.417 | Initialisation Mailer | ✅ OK |
| 21:55:04.453 | Queue mail créée | ✅ OK |
| 21:55:13.122 | Cible: oliv.arn.lau@gmail.com | ✅ OK |
| 21:55:13.150 | Mail ajouté à la queue | ✅ OK |
| 21:55:13.155 | "1 en attente" | ✅ OK |
| 21:55:13.166 | "Mail ENVOYÉ avec succès" | ⚠️ (ajout seulement) |
| 21:55:13.523 | Erreur TLS -10368 (HTTPS) | ❌ Problème mémoire |
| 21:55:13.590 | Erreur TLS -10368 (HTTPS) | ❌ Problème mémoire |
| **21:55:13 - 21:55:52** | **Aucune trace de `processOneMailSync()`** | ❌ **MANQUANT** |

---

## 💡 EXPLICATION PROBABLE

### Scénario le plus probable

1. **Le mail est ajouté à la queue** avec succès (21:55:13.150)

2. **`automationTask` tente de traiter le mail**, mais:
   - `TLSMutex::canConnect()` retourne `false` car:
     - Des opérations HTTPS sont en cours (fetchRemoteState)
     - Ces opérations échouent avec erreur -10368 (mémoire insuffisante)
     - Le mutex TLS reste verrouillé ou la condition `canConnect()` est false

3. **Le traitement est reporté**, mais:
   - Le monitoring s'arrête avant que le traitement ne réussisse
   - Ou le traitement n'est jamais déclenché car les conditions ne sont jamais remplies

### Erreur mémoire critique

L'erreur `-10368: X509 - Allocation of memory failed` indique que:
- Le heap libre est insuffisant pour établir une connexion TLS
- Les opérations HTTPS échouent
- Le système mail ne peut pas se connecter à SMTP (qui nécessite aussi TLS)

**Heap observé dans le log:**
- Avant TLS: ~77 KB
- Erreur TLS: -10368 (allocation échouée)
- Le système mail nécessite aussi de la mémoire pour TLS/SMTP

---

## ✅ SOLUTIONS PROPOSÉES

### Solution 1: Vérifier la configuration runtime
- Vérifier que `mailNotif` est activé dans NVS
- Vérifier que `_emailAddress` est bien configuré (actuellement utilise le fallback)

### Solution 2: Augmenter le heap disponible
- Réduire la fragmentation mémoire
- Libérer de la mémoire avant l'envoi de mail
- Augmenter la taille du heap libre minimum requis

### Solution 3: Améliorer le logging
- Ajouter des logs dans `hasPendingMails()` pour voir si elle retourne true
- Ajouter des logs dans `TLSMutex::canConnect()` pour voir pourquoi elle retourne false
- Ajouter des logs dans `processOneMailSync()` pour voir si elle est appelée

### Solution 4: Monitoring plus long
- Faire un monitoring de 5-10 minutes après le boot pour voir si le mail est traité plus tard
- Vérifier si le traitement se fait après que les opérations HTTPS soient terminées

### Solution 5: Mode synchrone pour le mail de boot
- Utiliser `sendAlertSync()` au lieu de `sendAlert()` pour le mail de démarrage
- Cela garantit l'envoi immédiat (mais bloque le boot)

---

## 📋 RÉSUMÉ

### Adresses Email
- **Expéditeur:** `arnould.svt@gmail.com` ✅
- **Destinataire:** `oliv.arn.lau@gmail.com` ✅

### Problème
- Le mail est **ajouté à la queue** mais **jamais traité**
- Aucun envoi SMTP réel n'a eu lieu
- Cause probable: **Erreur mémoire TLS (-10368)** empêchant le traitement

### Action requise
1. Vérifier le heap disponible au moment du traitement
2. Améliorer le logging pour diagnostiquer pourquoi `processOneMailSync()` n'est pas appelé
3. Faire un monitoring plus long pour voir si le traitement se fait plus tard
4. Considérer l'utilisation de `sendAlertSync()` pour le mail de boot

---

*Rapport généré automatiquement le 25 janvier 2026*

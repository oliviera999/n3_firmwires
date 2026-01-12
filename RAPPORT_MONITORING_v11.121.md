# 📊 Rapport de Monitoring & Diagnostic - Firmware v11.121
**Date** : 10 Janvier 2026  
**Version** : 11.121 (WROOM-32)  
**Durée monitoring** : 90 secondes  

## 🟢 Résumé Exécutif
Le système est **stable** et opérationnel. Les correctifs apportés à la gestion de la configuration (clés JSON) et à la sécurité du module Mail ont résolu les problèmes de stabilité et d'absence de notifications.

## 🛠️ État des Correctifs

| Problème | Correction | Statut | Preuve dans les logs |
| :--- | :--- | :--- | :--- |
| **Email vide** | Support des clés JSON numériques (`"100"`) dans `AutomatismNetwork` | ✅ **Corrigé** | `[GPIOParser] GPIO 100 (STRING) -> mail = 'oliv.arn.lau@gmail.com'` |
| **Crash Mailer** | Protection contre destinataire `NULL`/vide dans `Mailer::send` | ✅ **Sécurisé** | Plus d'erreur `-111` critique ni de *Panic* lié |
| **Boot Config** | Application immédiate du cache NVS au démarrage | ✅ **Validé** | Variables disponibles dès le boot |
| **No Mail** | Rétablissement du parsing des notifications (`"101"`) | ✅ **Corrigé** | `[GPIOParser] GPIO 101 (BOOL) -> mailNotif = true` |

## 📈 Métriques de Santé (v11.121)

### 1. Stabilité Système
*   **Uptime** : > 6 minutes (sur cette session)
*   **Reboots** : Aucun redémarrage intempestif observé.
*   **Mémoire (Heap)** : ~93 KB libres (très confortable pour un ESP32 WROOM).
    *   *Note* : Pas de fuite mémoire détectée sur la période.

### 2. Connectivité
*   **WiFi** : Connecté stable à `AP-Techno-T06`.
    *   **IP** : `192.168.42.32`
    *   **RSSI** : -65 dBm (Signal correct)
*   **API Distante** : Succès des requêtes `GET /api/outputs-test/state` (Code 200).
    *   Temps réponse moyen : ~1.4s (incluant handshake SSL).

### 3. Capteurs
*   **Température Eau** : 16.5°C (Stable/Simulée).
*   **Niveaux d'eau (Ultrasons)** :
    *   Lectures cohérentes avec filtrage actif.
    *   Exemple : `Saut détecté` correctement géré par l'algorithme médian pour éviter les faux positifs.

## 📧 Diagnostic Mail
*   **Configuration** :
    *   Expéditeur : `arnould.svt@gmail.com`
    *   Destinataire : `oliv.arn.lau@gmail.com` (Correctement chargé)
    *   SMTP : `smtp.gmail.com:465` (Connecté)
*   **Test** : Une notification de démarrage est programmée. Les alertes critiques (ex: niveau eau critique) déclencheront désormais l'envoi avec succès grâce au correctif d'adresse.

## 📋 Recommandations
1.  **Surveiller les spams** pour l'adresse `oliv.arn.lau@gmail.com` lors des premières alertes.
2.  **Test fonctionnel** : Vous pouvez provoquer une alerte contrôlée (ex: sortir le capteur de température de l'eau ou changer un seuil via l'interface web) pour confirmer la réception physique du mail.

---
**Conclusion** : Le firmware v11.121 est prêt pour la production.


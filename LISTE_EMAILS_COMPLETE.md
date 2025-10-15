# 📧 LISTE COMPLÈTE DES EMAILS ENVOYABLES - FFP5CS

**Environnements** : wroom-test, wroom-prod, s3-test, s3-prod  
**Total** : **27 types d'emails**  
**Fréquence totale** : ~652+ emails/jour en fonctionnement normal

---

## 📋 LISTE PAR CATÉGORIE

### 🚀 1. DÉMARRAGE (1 email)

| # | Sujet | Déclencheur | Fréquence |
|---|-------|-------------|-----------|
| 1 | `FFP3CS - Test de démarrage [HOSTNAME]` | Boot du système | 1×/boot |

**Contenu** : Version, date compilation, état capteurs/actionneurs, réseau

---

### 🍽️ 2. NOURRISSAGE (5 emails)

| # | Sujet | Déclencheur | Fréquence |
|---|-------|-------------|-----------|
| 2 | `FFP3 - Bouffe matin` | Horloge (01:00 par défaut) | 1×/jour |
| 3 | `FFP3 - Bouffe midi` | Horloge (13:00 par défaut) | 1×/jour |
| 4 | `FFP3 - Bouffe soir` | Horloge (19:00 par défaut) | 1×/jour |
| 5 | `FFP3 - Bouffe manuelle` | Action utilisateur (petits poissons) | À la demande |
| 6 | `FFP3 - Bouffe manuelle` | Action utilisateur (gros poissons) | À la demande |

**Contenu** : Heure, durées, état pompes, niveaux d'eau, température

---

### 🌊 3. ALERTES NIVEAUX D'EAU (4 emails)

| # | Sujet | Déclencheur | Fréquence |
|---|-------|-------------|-----------|
| 7 | `FFP3 - Alerte - Niveau aquarium BAS` | Distance > seuil (aqThreshold) | 1×/événement |
| 8 | `FFP3 - Alerte - Aquarium TROP PLEIN` | Distance < limFlood | Max 1×/30min (cooldown) |
| 9 | `FFP3 - Alerte - Réserve BASSE` | Distance > seuil (tankThreshold) | 1×/événement |
| 10 | `FFP3 - Info - Réserve OK` | Retour sous seuil | 1×/événement |

**Anti-spam** :
- **Aquarium trop plein** : Debounce 2min + Cooldown 30min + Hystérésis 5cm
- **Autres** : Flag + Hystérésis 5cm

---

### 💧 4. GESTION POMPE RÉSERVOIR (6 emails)

| # | Sujet | Déclencheur | Fréquence |
|---|-------|-------------|-----------|
| 11 | `Pompe réservoir MANUELLE ON` | Démarrage manuel | 1×/changement |
| 12 | `Pompe réservoir AUTO ON` | Démarrage auto | 1×/changement |
| 13 | `Pompe réservoir MANUELLE OFF` | Arrêt manuel | 1×/changement |
| 14 | `Pompe réservoir AUTO OFF` | Arrêt auto | 1×/changement |
| 15 | `Pompe réservoir OFF (sécurité réserve basse)` | Arrêt sécurité | 1×/événement |
| 16 | `Pompe réservoir OFF (sécurité aquarium trop plein)` | Arrêt sécurité | 1×/événement |

**Contenu** : Mode (manuel/auto), état, niveaux, durée fonctionnement, raison arrêt

---

### 🚫 5. ALERTES POMPE RÉSERVOIR (3 emails)

| # | Sujet | Déclencheur | Fréquence |
|---|-------|-------------|-----------|
| 17 | `FFP3 - Pompe réservoir BLOQUÉE (réserve basse)` | Réserve insuffisante | 1×/blocage |
| 18 | `FFP3 - Pompe réservoir bloquée` | Timeout mécanique | 1×/blocage |
| 19 | `FFP3 - Pompe réservoir verrouillée (réserve trop basse)` | Verrouillage sécurité | 1×/verrouillage |

**Contenu** : Raison, niveaux, seuils, procédure déblocage, diagnostics

---

### 🔵 6. GESTION POMPE AQUARIUM (2 emails)

| # | Sujet | Déclencheur | Fréquence |
|---|-------|-------------|-----------|
| 20 | `Pompe AQUARIUM ON (AUTO/MANUEL)` | Démarrage pompe aquarium | 1×/changement |
| 21 | `Pompe AQUARIUM OFF (AUTO/MANUEL)` | Arrêt pompe aquarium | 1×/changement |

---

### 🌡️ 7. CHAUFFAGE (2 emails)

| # | Sujet | Déclencheur | Fréquence |
|---|-------|-------------|-----------|
| 22 | `FFP3 - Chauffage ON` | Température < seuil (23°C) | 1×/activation |
| 23 | `FFP3 - Chauffage OFF` | Température ≥ seuil | 1×/désactivation |

**Contenu** : Température eau actuelle

---

### 😴 8. MISE EN VEILLE / RÉVEIL (2 emails)

| # | Sujet | Déclencheur | Fréquence |
|---|-------|-------------|-----------|
| 24 | `FFP3 - Mise en veille` | Entrée en light-sleep | ~576×/jour (150s) |
| 25 | `FFP3 - Réveil du système` | Sortie de light-sleep | ~72×/jour (1200s) |

**Conditions** : WiFi + emailEnabled + **heap ≥ 45KB**

**Contenu** :
- **Sleep** : Raison, durée prévue, état système, températures, niveaux, actionneurs
- **Wake** : Raison, durée réelle, état système, info réseau (WiFi, IP, RSSI)

**⚡ Optimisé v11.06** : Utilise cache (`_lastReadings`), pas de lecture capteurs

---

### 🔄 9. OTA (MISE À JOUR) (4 emails)

| # | Sujet | Déclencheur | Fréquence |
|---|-------|-------------|-----------|
| 26 | `FFP3 - OTA début - Serveur distant` | Début OTA automatique | 1×/OTA |
| 27 | `OTA mise à jour - Serveur distant [HOSTNAME]` | Succès OTA automatique | 1×/OTA |
| 28 | `OTA début - Interface web` | Début OTA manuel (web) | 1×/OTA |
| 29 | `OTA mise à jour - Interface web [HOSTNAME]` | Succès OTA manuel (web) | 1×/OTA |

**Contenu** : Méthode, versions (ancienne/nouvelle), URL firmware, taille, MD5, hostname

---

### 🧪 10. EMAILS MANUELS (2 emails)

| # | Sujet | Déclencheur | Fréquence |
|---|-------|-------------|-----------|
| 30 | `FFP3 - [SUJET PERSONNALISÉ]` | Endpoint `/mailtest` | À la demande |
| 31 | `FFP3 - Redémarrage GPIO` | Reset via GPIO 110 | 1×/reset manuel |

**Endpoint test** : `GET /mailtest?subject=XXX&body=YYY&to=ZZZ`

---

### 📊 11. DIGEST PÉRIODIQUE (1 email)

| # | Sujet | Déclencheur | Fréquence |
|---|-------|-------------|-----------|
| 32 | `FFP3CS - Digest événements [HOSTNAME]` | Timer 24h + nouveaux événements | Max 1×/24h |

**Contenu** :
- Résumé événements récents (EventLog)
- Marges stack FreeRTOS
- Dérive temporelle (Time Drift)
- Infos système

**Particularité** : Envoi SEULEMENT si nouveaux événements depuis dernier digest

---

## 📊 RÉCAPITULATIF STATISTIQUES

### Par fréquence

| Fréquence | Nombre de types | Emails/jour estimés |
|-----------|-----------------|---------------------|
| **Au boot** | 1 | 1 |
| **Quotidien fixe** | 3 (nourrissage) | 3 |
| **Haute fréquence** | 2 (sleep/wake) | **648** |
| **Par événement** | 19 | 10-35 |
| **À la demande** | 3 | Variable |
| **Périodique** | 1 (digest) | 1 |
| **TOTAL** | **32 types** | **~652-687/jour** |

### Par catégorie

| Catégorie | Nombre | % Total |
|-----------|--------|---------|
| Sleep/Wake | 2 | 99% du volume (648/652) |
| Nourrissage | 5 | <1% |
| Alertes capteurs | 4 | <1% |
| Pompes | 11 | <1% |
| Chauffage | 2 | <1% |
| OTA | 4 | <1% |
| Autres | 4 | <1% |

---

## ⚙️ CONDITIONS GLOBALES D'ENVOI

Pour qu'**UN EMAIL SOIT ENVOYÉ**, il faut (cumulatif) :

### ✅ Compilation
```ini
-DFEATURE_MAIL=1  # Dans platformio.ini
```

### ✅ Runtime
```cpp
1. WiFi.status() == WL_CONNECTED          // WiFi connecté
2. autoCtrl.isEmailEnabled() == true      // Notifications activées
3. ESP.getFreeHeap() >= 45000             // 45KB min (sleep/wake uniquement)
4. autoCtrl.getEmailAddress() != ""       // Adresse configurée
```

### 🔐 Configuration SMTP
```cpp
Serveur : smtp.gmail.com:465 (SSL)
Compte  : arnould.svt@gmail.com
Auth    : App Password Gmail
```

---

## 📈 VOLUMÉTRIE DÉTAILLÉE (24 HEURES)

### Scénario NOMINAL (sans alertes)

| Type d'email | Fréquence | Nombre/jour |
|--------------|-----------|-------------|
| Boot test | 1×/boot | 1 |
| Nourrissage (matin/midi/soir) | 3×/jour | 3 |
| **Sleep** | ~576×/jour | **576** |
| **Wake** | ~72×/jour | **72** |
| Digest | 1×/24h | 1 |
| **TOTAL NOMINAL** | | **653** |

### Scénario AVEC ALERTES (modéré)

| Type d'email | Ajout estimé/jour |
|--------------|-------------------|
| Aquarium BAS | +2 |
| Aquarium TROP PLEIN | +1 à +48 (cooldown 30min) |
| Réserve BASSE/OK | +2 |
| Pompe réservoir ON/OFF | +10 |
| Chauffage ON/OFF | +20 |
| **TOTAL AVEC ALERTES** | **~688** |

### ⚠️ DÉPASSEMENT QUOTA GMAIL

**Quota Gmail standard** : 500 emails/jour  
**Volume système** : 652-688 emails/jour  
**→ DÉPASSEMENT** : **~150-188 emails/jour** 🚨

---

## 💡 RECOMMANDATIONS OPTIMISATION

### 🔴 CRITIQUE : Réduire emails Sleep/Wake (648/jour)

**Option 1** : Désactiver complètement (économise 648 emails/jour)
```cpp
// Dans src/automatism.cpp, commenter les appels :
// _mailer.sendSleepMail(...)
// _mailer.sendWakeMail(...)
```

**Option 2** : Augmenter fréquence (recommandé)
```cpp
freqWakeSec = 3600;  // 1 heure au lieu de 20min
// → 24 wake/jour + 96 sleep/jour = 120 emails/jour au lieu de 648
// → TOTAL : 125 emails/jour (dans quota)
```

**Option 3** : Sleep/Wake uniquement si événements
```cpp
// Envoyer seulement si alertes ou changements importants
```

### 🟡 OPTIONNEL : Réduire emails secondaires

**Chauffage ON/OFF** : Si non critique
```cpp
// Envoyer seulement si température anormale (< 20°C ou > 28°C)
```

**Digest** : Déjà optimisé (seulement si nouveaux événements)

---

## 📋 LISTE RAPIDE (NOM + SUJET)

```
1.  Boot                    → FFP3CS - Test de démarrage [HOSTNAME]
2.  Nourrissage matin       → FFP3 - Bouffe matin
3.  Nourrissage midi        → FFP3 - Bouffe midi
4.  Nourrissage soir        → FFP3 - Bouffe soir
5.  Nourrissage manuel (p)  → FFP3 - Bouffe manuelle
6.  Nourrissage manuel (g)  → FFP3 - Bouffe manuelle
7.  Aquarium BAS            → FFP3 - Alerte - Niveau aquarium BAS
8.  Aquarium TROP PLEIN     → FFP3 - Alerte - Aquarium TROP PLEIN
9.  Réserve BASSE           → FFP3 - Alerte - Réserve BASSE
10. Réserve OK              → FFP3 - Info - Réserve OK
11. Pompe réserv MANUEL ON  → Pompe réservoir MANUELLE ON
12. Pompe réserv AUTO ON    → Pompe réservoir AUTO ON
13. Pompe réserv MANUEL OFF → Pompe réservoir MANUELLE OFF
14. Pompe réserv AUTO OFF   → Pompe réservoir AUTO OFF
15. Pompe réserv OFF sécu 1 → Pompe réservoir OFF (sécurité réserve basse)
16. Pompe réserv OFF sécu 2 → Pompe réservoir OFF (sécurité aquarium trop plein)
17. Pompe réserv BLOQUÉE    → FFP3 - Pompe réservoir BLOQUÉE (réserve basse)
18. Pompe réserv timeout    → FFP3 - Pompe réservoir bloquée
19. Pompe réserv VERROUILLÉE→ FFP3 - Pompe réservoir verrouillée (réserve trop basse)
20. Pompe aqua ON           → Pompe AQUARIUM ON (AUTO/MANUEL)
21. Pompe aqua OFF          → Pompe AQUARIUM OFF (AUTO/MANUEL)
22. Chauffage ON            → FFP3 - Chauffage ON
23. Chauffage OFF           → FFP3 - Chauffage OFF
24. Sleep                   → FFP3 - Mise en veille (576×/jour)
25. Wake                    → FFP3 - Réveil du système (72×/jour)
26. OTA début serveur       → FFP3 - OTA début - Serveur distant
27. OTA succès serveur      → OTA mise à jour - Serveur distant [HOSTNAME]
28. OTA début web           → OTA début - Interface web
29. OTA succès web          → OTA mise à jour - Interface web [HOSTNAME]
30. Email test manuel       → FFP3 - [SUJET PERSONNALISÉ]
31. Reset GPIO              → FFP3 - Redémarrage GPIO
32. Digest                  → FFP3CS - Digest événements [HOSTNAME]
```

---

## ✅ CHECKLIST ACTIVATION

Pour activer les emails sur un environnement :

- [ ] `FEATURE_MAIL=1` dans `platformio.ini`
- [ ] Bibliothèque `ESP Mail Client` dans `lib_deps`
- [ ] WiFi configuré et connecté
- [ ] Variable serveur `mailNotif=checked`
- [ ] Variable serveur `mail=adresse@email.com`
- [ ] Credentials SMTP dans `include/secrets.h`
- [ ] Port 465 ouvert sur le réseau

---

**Document généré** : 2025-10-14  
**Total emails** : 32 types  
**Volume quotidien** : 652-688 emails/jour  
**⚠️ ATTENTION** : Dépasse quota Gmail (500/jour) → Optimisation recommandée




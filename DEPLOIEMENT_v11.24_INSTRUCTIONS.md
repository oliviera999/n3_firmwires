# 🚀 Instructions de Déploiement v11.24

## 📌 Contexte

**Problème corrigé** : Erreur `ERR_CONNECTION_RESET` lors du chargement de `/shared/common.js`  
**Version** : 11.23 → **11.24**  
**Type** : Bugfix critique (UX)  
**Prérequis** : ESP32 accessible sur le réseau (192.168.0.86)

## ⚡ Déploiement Rapide

### Option 1 : Commandes Directes (Recommandé)

```powershell
# 1. Se positionner dans le projet
cd "C:\Users\olivi\Mon Drive\travail\##olution\##Projets\##prototypage\platformIO\Projects\ffp5cs"

# 2. Upload firmware + filesystem en une seule commande
pio run -e wroom-test -t upload -t uploadfs --upload-port 192.168.0.86

# 3. Monitoring
pio device monitor --baud 115200
```

### Option 2 : Upload Séparé

```powershell
# 1. Firmware uniquement
pio run -e wroom-test -t upload --upload-port 192.168.0.86

# 2. Attendre redémarrage (~10 secondes)

# 3. Filesystem uniquement
pio run -e wroom-test -t uploadfs --upload-port 192.168.0.86

# 4. Attendre redémarrage (~10 secondes)

# 5. Monitoring
pio device monitor --baud 115200
```

## 📋 Checklist Pré-Déploiement

- [x] ✅ Code compilé sans erreur
- [x] ✅ Filesystem construit
- [x] ✅ Version incrémentée (11.24)
- [ ] 🔵 ESP32 accessible sur le réseau
- [ ] 🔵 Connexion série disponible (optionnel mais recommandé)

## 🧪 Procédure de Test Complète

### 1. Monitoring Série (90 secondes)

**Démarrer monitoring** :
```powershell
# PowerShell
.\monitor_15min.ps1

# Ou via PlatformIO
pio device monitor --baud 115200 --filter send_on_enter --filter colorize
```

**Logs critiques à surveiller** :
```
[Web] 📊 Heap libre avant common.js: XXXXX bytes      # Doit être > 30000
[Web] 📏 common.js size: 39962 bytes
[Web] ✅ common.js envoyé (heap libre: XXXXX bytes)
[Web] 📊 Heap libre avant websocket.js: XXXXX bytes   # Doit être > 30000
[Web] ✅ websocket.js envoyé (heap libre: XXXXX bytes)
```

**❌ Erreurs critiques** :
```
[Web] ⚠️ Mémoire insuffisante pour servir common.js  # Code 503 envoyé
[Web] ❌ Échec beginResponse pour common.js          # Problème LittleFS
Guru Meditation Error                                 # Crash système
```

### 2. Test Navigateur

#### a) Vider le cache
```
Chrome/Edge : Ctrl + Shift + Delete → "Images et fichiers en cache"
Firefox     : Ctrl + Shift + Delete → "Cache"
```

#### b) Accéder au dashboard
```
URL: http://192.168.0.86/
```

#### c) Ouvrir DevTools (F12)
**Onglet Console** - Logs attendus :
```javascript
✅ Script chargé: /shared/common.js
✅ Script chargé: /shared/websocket.js
✅ Tous les scripts critiques chargés
[INIT] ℹ️ Dashboard consolidé initialisé
🏓 Ping reçu - Réveil système
```

**Onglet Network** - Vérifications :
- ✅ `/shared/common.js` : Status `200 OK`, Size `~40 KB`
- ✅ `/shared/websocket.js` : Status `200 OK`, Size `~54 KB`
- ✅ Aucun status `503` ou `ERR_CONNECTION_RESET`

**Onglet Application > Local Storage** :
- Vider si nécessaire pour forcer rechargement

### 3. Tests Fonctionnels

#### a) Chargement initial
- [ ] ✅ Page affichée en < 3 secondes
- [ ] ✅ Badge vert "En ligne" visible
- [ ] ✅ Données capteurs affichées
- [ ] ✅ Graphiques tracés

#### b) Rechargements multiples
```
- Recharger 5 fois (F5)
- Pas d'erreur console
- Temps de chargement stable
```

#### c) Navigation
- [ ] ✅ Onglet "WiFi" fonctionne
- [ ] ✅ Onglet "Contrôles" fonctionne  
- [ ] ✅ Retour "Mesures" fonctionne

#### d) WebSocket
- [ ] ✅ Connexion établie (badge vert)
- [ ] ✅ Données mises à jour en temps réel
- [ ] ✅ Pas de déconnexion intempestive

### 4. Test Retry Automatique (Optionnel)

**Simulation d'erreur réseau** :
1. Ouvrir DevTools → Network
2. Activer "Offline" temporairement
3. Recharger page (F5)
4. Désactiver "Offline" rapidement (< 2s)

**Résultat attendu** :
```javascript
⚠️ Échec chargement script: /shared/common.js, tentatives restantes: 2
⚠️ Échec chargement script: /shared/common.js, tentatives restantes: 1
✅ Script chargé: /shared/common.js
```

## 📊 Critères de Succès

### ✅ Déploiement Réussi
- ✅ Aucune erreur dans logs série (90s)
- ✅ Aucune erreur console navigateur
- ✅ Dashboard fonctionnel
- ✅ WebSocket connecté
- ✅ Données en temps réel
- ✅ Navigation fluide
- ✅ Heap libre > 30KB pendant service fichiers

### ⚠️ Surveillance Continue
**Pendant les 24 premières heures** :
- Surveiller logs pour codes `503` répétés
- Vérifier absence de resets ESP32 non planifiés
- Tester chargement page depuis différents navigateurs
- Tester rechargements multiples

## 🚨 Dépannage

### Problème : Code 503 "Service temporairement indisponible"

**Diagnostic** :
```
[Web] ⚠️ Mémoire insuffisante pour servir common.js
[Web] 📊 Heap libre avant common.js: 25000 bytes  # < 30000
```

**Solution immédiate** :
1. Le retry automatique devrait réussir après 1-2 secondes
2. Si échec répété : redémarrer ESP32
3. Si persistant : augmenter le seuil mémoire dans code

**Code à modifier** (si nécessaire) :
```cpp
// src/web_server.cpp ligne ~235
if (freeHeap < 50000) { // Au lieu de 30000
```

### Problème : ERR_CONNECTION_RESET persiste

**Vérifications** :
1. Filesystem uploadé ? `pio run -e wroom-test -t uploadfs`
2. Fichier existe ? Vérifier dans LittleFS
3. Taille correcte ? Doit être ~40KB

**Test direct** :
```bash
curl -v http://192.168.0.86/shared/common.js
```

### Problème : "Impossible de charger après plusieurs tentatives"

**Cause possible** :
- ESP32 hors ligne
- WiFi instable
- LittleFS corrompu

**Actions** :
1. Vérifier ping : `ping 192.168.0.86`
2. Reflasher filesystem complet
3. Vérifier logs série pour erreurs LittleFS

### Problème : isInitialized is not defined (encore)

**Cause** : Cache navigateur
**Solution** :
```
1. Ctrl + Shift + Delete → Vider cache
2. Ctrl + F5 (rechargement forcé)
3. Fermer/Rouvrir navigateur
```

## 📝 Après Déploiement

### 1. Documentation
```markdown
Mettre à jour VERSION.md :
- Date : 2025-10-13
- Version : 11.24
- Changement : Fix ERR_CONNECTION_RESET common.js
- Résultat tests : OK / Problèmes rencontrés
```

### 2. Git Commit
```bash
git add .
git commit -m "v11.24: Fix ERR_CONNECTION_RESET pour common.js avec routes dédiées et retry automatique"
git push
```

### 3. Notes de Monitoring
```
Créer un fichier : MONITORING_v11.24_YYYY-MM-DD.md
Contenu :
- Timestamp déploiement
- Résumé logs 90s
- Erreurs détectées
- Actions correctives si nécessaire
```

## 📞 Support

### Logs à Fournir en Cas de Problème
1. Logs série complets (90 secondes minimum)
2. Capture écran console navigateur (onglets Console + Network)
3. Résultat `curl -v http://192.168.0.86/shared/common.js`
4. État mémoire : `http://192.168.0.86/server-status`

### Commandes de Diagnostic
```powershell
# État système
curl http://192.168.0.86/server-status

# Test direct fichiers
curl -I http://192.168.0.86/shared/common.js
curl -I http://192.168.0.86/shared/websocket.js

# Vérifier filesystem
pio run -e wroom-test -t buildfs --verbose
```

## ✅ Validation Finale

**Après 24h d'utilisation** :
- [ ] Aucun crash ESP32
- [ ] Aucune erreur répétée dans logs
- [ ] Dashboard toujours accessible
- [ ] Performance stable
- [ ] Mémoire stable (> 50KB free en moyenne)

**Si tous les critères OK** → Version 11.24 validée ✅

---

**Préparé le** : 2025-10-13  
**Version** : 11.24  
**Auteur** : Correction automatisée ERR_CONNECTION_RESET  
**Durée estimée déploiement** : 5 minutes  
**Durée monitoring** : 90 secondes minimum


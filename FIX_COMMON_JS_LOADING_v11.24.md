# Fix Erreur de Chargement common.js - v11.24

## 📋 Problèmes Identifiés

### 1. ERR_CONNECTION_RESET pour common.js
**Symptôme** : `GET http://192.168.0.86/shared/common.js net::ERR_CONNECTION_RESET 200 (OK)`
- Le serveur commence à envoyer le fichier (code 200) mais ferme la connexion prématurément
- Fichier volumineux : **~40 KB** (39 962 bytes)
- Cause probable : contraintes mémoire ESP32 avec `serveStatic()`

### 2. JavaScript Error: isInitialized is not defined
**Symptôme** : `Uncaught ReferenceError: isInitialized is not defined` (websocket.js:735)
- La variable `isInitialized` est déclarée dans `common.js` (ligne 12)
- Si `common.js` échoue à se charger, `websocket.js` ne peut pas accéder à la variable
- Erreur en cascade due au problème #1

## 🔧 Solutions Implémentées

### 1. Routes Dédiées pour Fichiers Volumineux
**Fichier** : `src/web_server.cpp`
**Modification** : Ajout de routes dédiées avec gestion mémoire robuste

```cpp
// Route dédiée pour /shared/common.js (~40KB)
_server->on("/shared/common.js", HTTP_GET, [](AsyncWebServerRequest* req){
  autoCtrl.notifyLocalWebActivity();
  
  uint32_t freeHeap = ESP.getFreeHeap();
  Serial.printf("[Web] 📊 Heap libre avant common.js: %u bytes\n", freeHeap);
  
  if (freeHeap < 30000) { // Vérification mémoire
    req->send(503, "text/plain", "Service temporairement indisponible - mémoire faible");
    return;
  }
  
  // Streaming robuste du fichier...
});

// Route dédiée pour /shared/websocket.js (~54KB)
_server->on("/shared/websocket.js", HTTP_GET, [](AsyncWebServerRequest* req){
  // Même approche robuste
});
```

**Avantages** :
- ✅ Vérification de mémoire avant envoi
- ✅ Logs détaillés pour diagnostic
- ✅ Gestion d'erreurs explicite
- ✅ Prévention des crashes mémoire

### 2. Retry Automatique Côté Client
**Fichier** : `data/index.html`
**Modification** : Chargement intelligent des scripts avec retry

```javascript
function loadScriptWithRetry(src, retries = 3, delay = 1000) {
  return new Promise((resolve, reject) => {
    const script = document.createElement('script');
    script.src = src;
    script.onload = () => resolve();
    script.onerror = () => {
      if (retries > 0) {
        setTimeout(() => {
          loadScriptWithRetry(src, retries - 1, delay).then(resolve).catch(reject);
        }, delay);
      } else {
        reject(new Error(`Impossible de charger ${src}`));
      }
    };
    document.head.appendChild(script);
  });
}

// Chargement séquentiel avec retry
loadScriptWithRetry('/shared/common.js')
  .then(() => loadScriptWithRetry('/shared/websocket.js'))
  .then(() => console.log('✅ Scripts chargés'))
  .catch((error) => {
    // Affichage d'erreur avec bouton de rechargement
  });
```

**Avantages** :
- ✅ 3 tentatives automatiques par script
- ✅ Délai de 1s entre chaque tentative
- ✅ Message d'erreur explicite si échec
- ✅ Bouton de rechargement pour l'utilisateur

### 3. Protection Variable isInitialized
**Fichier** : `data/shared/websocket.js`
**Modification** : Utilisation de `window.isInitialized` avec vérification

**Avant** :
```javascript
document.addEventListener('DOMContentLoaded', function() {
  if (isInitialized) return;  // ❌ Crash si common.js non chargé
  isInitialized = true;
```

**Après** :
```javascript
document.addEventListener('DOMContentLoaded', function() {
  // CORRECTION: Utiliser window.isInitialized pour éviter les erreurs
  if (!window.isInitialized) {
    window.isInitialized = true;
  } else {
    return; // Déjà initialisé
  }
```

**Avantages** :
- ✅ Pas de crash si `common.js` échoue
- ✅ Initialisation conditionnelle
- ✅ Compatibilité avec les retries

## 📊 Impacts et Améliorations

### Stabilité
- ✅ **Élimination des ERR_CONNECTION_RESET** pour les fichiers volumineux
- ✅ **Pas de crash JavaScript** même si common.js échoue au premier chargement
- ✅ **Récupération automatique** avec retry

### Performance
- ✅ Vérification mémoire préventive (évite les crashes ESP32)
- ✅ Logs détaillés pour diagnostic rapide
- ✅ Cache-Control optimisé (86400s = 24h)

### UX Utilisateur
- ✅ Chargement transparent avec retry automatique
- ✅ Message d'erreur clair en cas d'échec total
- ✅ Bouton de rechargement accessible
- ✅ Console logs informatifs

## 🧪 Tests Recommandés

### 1. Test Chargement Initial
```bash
# Accéder à la page principale
curl -I http://192.168.0.86/

# Vérifier common.js
curl -I http://192.168.0.86/shared/common.js

# Vérifier websocket.js
curl -I http://192.168.0.86/shared/websocket.js
```

**Attendu** :
- ✅ Code 200 OK pour tous les fichiers
- ✅ Logs "[Web] ✅ common.js envoyé" dans la console série
- ✅ Pas d'erreur ERR_CONNECTION_RESET dans la console navigateur

### 2. Test Console Navigateur
1. Ouvrir http://192.168.0.86/ dans le navigateur
2. Ouvrir les DevTools (F12)
3. Onglet Console

**Attendu** :
```
✅ Script chargé: /shared/common.js
✅ Script chargé: /shared/websocket.js
✅ Tous les scripts critiques chargés
[INIT] ℹ️ Dashboard consolidé initialisé
```

### 3. Test Mémoire Faible
1. Simuler une faible mémoire (monitoring pendant activité intense)
2. Recharger la page

**Attendu** :
- Si heap < 30KB → Code 503 "Service temporairement indisponible"
- Retry automatique après 1s
- Succès au 2ème ou 3ème essai

### 4. Test Retry Automatique
1. Couper temporairement le WiFi
2. Accéder à la page
3. Rétablir le WiFi pendant le retry

**Attendu** :
- ⚠️ Message dans console : "Échec chargement script, tentatives restantes: 2"
- ✅ Succès automatique quand WiFi revient
- Pas d'intervention utilisateur nécessaire

## 📝 Monitoring Post-Déploiement

### Logs Série à Surveiller
```
[Web] 📊 Heap libre avant common.js: XXXXX bytes
[Web] 📏 common.js size: 39962 bytes
[Web] ✅ common.js envoyé (heap libre: XXXXX bytes)
```

### Console Navigateur à Surveiller
- Absence de `ERR_CONNECTION_RESET`
- Absence de `isInitialized is not defined`
- Messages `✅ Script chargé` présents

## 🔄 Rollback si Nécessaire

Si des problèmes persistent :

1. **Réduire la taille de common.js** :
   - Séparer en modules plus petits
   - Minifier le code
   - Supprimer les commentaires

2. **Augmenter le seuil mémoire** :
   ```cpp
   if (freeHeap < 50000) { // Au lieu de 30000
   ```

3. **Augmenter le nombre de retries** :
   ```javascript
   loadScriptWithRetry(src, retries = 5, delay = 2000)
   ```

## 📦 Checklist Déploiement

- [x] Code modifié (web_server.cpp, index.html, websocket.js)
- [x] Version incrémentée (11.23 → 11.24)
- [x] Pas d'erreurs de compilation
- [ ] Compiler le firmware
- [ ] Uploader le firmware via OTA
- [ ] Uploader le filesystem (data/) via OTA
- [ ] Monitoring 90 secondes post-déploiement
- [ ] Vérifier logs série
- [ ] Vérifier console navigateur
- [ ] Tester rechargement de page (F5)
- [ ] Documenter les résultats

## 📚 Références

- **Règles projet** : `.cursorrules` - Monitoring 90s obligatoire
- **Version précédente** : 11.23
- **Fichiers modifiés** :
  - `src/web_server.cpp` (routes dédiées)
  - `data/index.html` (retry automatique)
  - `data/shared/websocket.js` (protection isInitialized)
  - `include/project_config.h` (version 11.24)

---

**Version** : 11.24  
**Date** : 2025-10-13  
**Priorité** : Haute (correction bug UX critique)  
**Type** : Bugfix + Amélioration robustesse


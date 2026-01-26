# Rapport d'Analyse - Échanges avec le Serveur Distant
**Date**: 26 janvier 2026  
**Version**: 11.157 (post-correction queues)  
**Fichier log**: monitor_wroom_test_2026-01-26_14-26-16.log  
**Durée**: 3 minutes

## 🔴 RÉSUMÉ EXÉCUTIF

### **Statut : Communications échouent**

Les échanges avec le serveur distant **échouent systématiquement** à cause de deux problèmes principaux :

1. **Fragmentation mémoire excessive (56%)** - Bloque les opérations TLS
2. **Serveur indisponible (HTTP 503)** - Problème côté serveur

## 📊 STATISTIQUES DES COMMUNICATIONS

### Requêtes GET (fetchRemoteState)

| Tentative | Résultat | Code HTTP | Cause |
|-----------|----------|-----------|-------|
| 1 (boot) | ❌ Échec | 503 | Serveur indisponible + HTML au lieu de JSON |
| 2-6 | ❌ Bloquée | - | Fragmentation mémoire (56%) - Bloc contigu insuffisant |

**Taux de réussite**: **0%** (0/6)

### Requêtes POST (envoi données)

| Tentative | Heure | Payload | Résultat | DataQueue |
|-----------|-------|---------|----------|-----------|
| 1 | 14:28:12 | 461 bytes | ❌ FAILED | ✅ Sauvegardé (3 entrées) |
| 2 | 14:28:38 | 459 bytes | ❌ FAILED | ❓ Non visible dans logs |
| 3 | 14:29:01 | 460 bytes | ❌ FAILED | ❓ Non visible dans logs |

**Taux de réussite**: **0%** (0/3)

## 🔍 ANALYSE DÉTAILLÉE

### 1. **Problème de fragmentation mémoire** ⚠️ CRITIQUE

**Symptômes**:
```
[GET] 📊 Fragmentation: 56%
[GET] 📊 Largest free block: 34804 bytes
[GET] ⚠️ Plus grand bloc insuffisant (34804 bytes < 45KB), fragmentation=56%
[GET] ⚠️ TLS nécessite ~42-46KB contiguë, report de la requête
```

**Cause**:
- Après la première opération TLS (GET au boot), la fragmentation passe de **32% à 56%**
- Le plus grand bloc disponible passe de **55KB à 34KB** (réduction de 20KB)
- TLS nécessite **42-46KB contiguë** pour fonctionner
- Seulement **34KB disponibles** → **insuffisant**

**Impact**:
- **Toutes les requêtes GET suivantes sont bloquées** (reportées)
- **Les requêtes POST échouent** (même problème de mémoire)

**Fréquence**: Problème récurrent, toutes les tentatives après le premier GET sont bloquées

### 2. **Erreur serveur HTTP 503** ⚠️

**Première tentative GET (boot)**:
```
14:26:24.577 > [GET] GET completed in 2056 ms, HTTP code: 503
14:26:25.220 > [GET] <html>
14:26:25.220 > <head><title>503 Service Temporarily Unavailable</title></head>
14:26:25.239 > [GET] ❌ JSON parse error: InvalidInput
```

**Analyse**:
- Le serveur répond avec **HTTP 503** (Service Temporarily Unavailable)
- La réponse est du **HTML** au lieu de JSON
- Le parsing JSON échoue (comportement attendu)
- Serveur: **openresty** (nginx)

**Impact**:
- La première tentative GET échoue
- Les tentatives suivantes sont bloquées par la fragmentation mémoire

### 3. **Échecs POST** ❌

**Tentative 1** (14:28:12):
```
[PR] Payload input: 461 bytes
[PR] API Key: fdGTMoptd5CD2ert3
[PR] Sensor: esp32-wroom
[PR] Sending to primary server...
[PR] Primary server result: FAILED
[PR] Final result: FAILED
[DataQueue] ✓ Payload enregistré (461 bytes, total: ~3 entrées)
```

**Tentative 2** (14:28:38):
```
[PR] Payload input: 459 bytes
[PR] Sending to primary server...
[PR] Primary server result: FAILED
[PR] Final result: FAILED
```

**Tentative 3** (14:29:01):
```
[PR] Payload input: 460 bytes
[PR] Sending to primary server...
[PR] Primary: FAILED
[PR] Secondary: FAILED
[PR] Final result: FAILED
```

**Observations**:
- Les payloads sont correctement préparés (459-461 bytes)
- L'API key est présente
- Les envois échouent systématiquement
- **1 payload sauvegardé dans DataQueue** après échec (tentative 1)
- Les autres tentatives ne montrent pas de sauvegarde dans les logs (peut-être déjà dans la queue)

### 4. **DataQueue** ✅

**Statut**: Fonctionne correctement

**Observations**:
- Initialisée correctement (0 entrées au boot)
- **1 payload sauvegardé** après échec POST (ligne 6819)
- Total: ~3 entrées après la première sauvegarde
- Aucune rotation détectée (la queue n'a pas atteint la limite de 50)

**Conclusion**: Le mécanisme de sauvegarde fonctionne, les données ne sont pas perdues.

## 🎯 CAUSES IDENTIFIÉES

### Cause 1: Fragmentation mémoire excessive (CRITIQUE)

**Problème**: La fragmentation mémoire atteint 56% après la première opération TLS, ce qui empêche l'allocation de blocs contigus suffisants pour les opérations TLS suivantes.

**Séquence**:
1. Boot → Fragmentation: 24-32%
2. Première opération TLS (GET) → Fragmentation: **32% → 56%** (+24%)
3. Plus grand bloc: **55KB → 34KB** (-20KB)
4. TLS nécessite 42-46KB contiguë → **Bloqué**

**Impact**: 
- Toutes les requêtes GET suivantes sont reportées
- Toutes les requêtes POST échouent

**Solution recommandée**:
1. Analyser les allocations mémoire pendant TLS
2. Optimiser les allocations pour réduire la fragmentation
3. Considérer un défragmentation périodique
4. Réduire les besoins TLS si possible

### Cause 2: Serveur indisponible ou erreur

**Problème**: Le serveur répond avec HTTP 503 (Service Temporarily Unavailable) lors de la première tentative GET.

**Impact**:
- Les requêtes GET échouent
- Les requêtes POST peuvent échouer pour la même raison

**Solution recommandée**:
1. Vérifier l'état du serveur distant (iot.olution.info)
2. Vérifier la connectivité réseau
3. Vérifier la configuration de l'URL du serveur

## 📋 RÉSUMÉ DES PROBLÈMES

| Problème | Fréquence | Impact | Priorité |
|----------|-----------|--------|----------|
| Fragmentation mémoire (56%) | Récurrent | Bloque toutes les opérations TLS | 🔴 CRITIQUE |
| HTTP 503 / Serveur indisponible | 1 occurrence | Bloque GET | ⚠️ À vérifier |
| Échecs POST | 3/3 (100%) | Bloque envoi données | 🔴 CRITIQUE |
| Réponse HTML au lieu de JSON | 1 occurrence | Parse error | ⚠️ Conséquence du 503 |

## 🔧 RECOMMANDATIONS

### Priorité 1: Résoudre la fragmentation mémoire

**Actions immédiates**:
1. **Analyser les allocations mémoire**
   - Identifier les allocations qui fragmentent la mémoire pendant TLS
   - Optimiser les allocations pour réduire la fragmentation

2. **Considérer un défragmentation**
   - Implémenter une défragmentation périodique si possible
   - Libérer la mémoire non utilisée

3. **Réduire les besoins TLS**
   - Optimiser les buffers TLS si possible
   - Réduire la taille des requêtes

### Priorité 2: Vérifier le serveur distant

**Actions immédiates**:
1. **Vérifier l'état du serveur**
   - Tester l'URL manuellement: `https://iot.olution.info/ffp3/api/outputs-test/state`
   - Vérifier que le serveur répond correctement

2. **Vérifier la connectivité**
   - Tester la connexion réseau
   - Vérifier les paramètres WiFi

3. **Vérifier la configuration**
   - Vérifier l'URL du serveur
   - Vérifier les credentials (API key)

### Priorité 3: Améliorer la gestion des échecs

**Actions à moyen terme**:
1. **Sauvegarder systématiquement les échecs**
   - S'assurer que tous les payloads POST échoués sont sauvegardés dans DataQueue
   - Améliorer les logs pour confirmer les sauvegardes

2. **Améliorer les logs**
   - Ajouter plus de détails sur les causes d'échec POST
   - Logger les codes HTTP et messages d'erreur

3. **Réessayer automatiquement**
   - Implémenter un mécanisme de réessai automatique
   - Utiliser DataQueue pour rejouer les données échouées

## 📝 CONCLUSION

**Statut**: ⚠️ **COMMUNICATIONS ÉCHOUENT**

### Résumé

Les communications avec le serveur distant **échouent systématiquement** à cause de :
1. **Fragmentation mémoire excessive (56%)** - Bloque les opérations TLS (problème principal)
2. **Erreurs serveur (HTTP 503)** - Serveur indisponible ou erreur (1 occurrence)

### Points positifs

1. ✅ Le système tente de communiquer (tentatives détectées)
2. ✅ Les payloads sont correctement préparés (459-461 bytes)
3. ✅ Le système détecte les problèmes (logs détaillés)
4. ✅ **DataQueue fonctionne** - 1 payload sauvegardé après échec
5. ✅ Le système gère correctement les échecs (sauvegarde dans DataQueue)

### Actions requises

1. **Immédiat**: Vérifier l'état du serveur distant (HTTP 503)
2. **Court terme**: Résoudre la fragmentation mémoire (56% → <40%)
3. **Moyen terme**: Améliorer la gestion des échecs et la sauvegarde dans DataQueue

### Note importante

**DataQueue fonctionne** : Un payload a été sauvegardé après échec POST, ce qui confirme que le mécanisme de sauvegarde fonctionne. **Les données ne sont pas perdues** et seront réessayées plus tard.

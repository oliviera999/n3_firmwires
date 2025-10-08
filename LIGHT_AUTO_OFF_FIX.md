# 🔦 FIX : Lumière qui s'éteint toute seule

## 📋 Résumé du problème

**Symptôme:** La lumière s'éteint automatiquement quelques secondes après avoir été allumée manuellement depuis l'interface web.

**Cause racine:** Désynchronisation entre deux variables dans la base de données :
- L'ESP32 envoie `etatUV=1` quand la lumière est allumée
- Le serveur PHP renvoie `GPIO 15=0` (ancienne valeur non synchronisée)
- L'ESP32 obéit à GPIO 15 et éteint la lumière

## 🔍 Analyse technique

### Flux problématique

```
1. Utilisateur allume lumière via web → startLightManualLocal()
2. ESP32 allume GPIO 15 localement
3. ESP32 envoie au serveur: etatUV=1
4. Serveur met à jour etatUV dans BDD
5. ❌ MAIS serveur ne met PAS à jour GPIO 15
6. ESP32 redemande état au serveur → handleRemoteState()
7. Serveur renvoie: GPIO 15 = 0 (ancienne valeur)
8. ESP32 éteint la lumière (obéit au serveur)
```

### Code concerné

**Fichier:** `src/automatism.cpp`

**Lignes 247-321:** Fonctions `startLightManualLocal()` et `stopLightManualLocal()`

**Ancien code (ligne 268):**
```cpp
bool success = autoCtrl.sendFullUpdate(freshReadings, "etatUV=1");
```

**Problème:** N'envoie que `etatUV`, pas le GPIO 15.

## ✅ Solution implémentée

### Modification 1 : Allumage lumière (ligne 269)

```cpp
// CORRECTION: Envoyer à la fois etatUV ET GPIO 15 pour synchronisation complète
bool success = autoCtrl.sendFullUpdate(freshReadings, "etatUV=1&15=1");
```

### Modification 2 : Extinction lumière (ligne 308)

```cpp
// CORRECTION: Envoyer à la fois etatUV ET GPIO 15 pour synchronisation complète
bool success = autoCtrl.sendFullUpdate(freshReadings, "etatUV=0&15=0");
```

## 🎯 Résultat attendu

Après cette modification :

1. ✅ Allumage manuel → Envoie `etatUV=1&15=1`
2. ✅ Serveur met à jour les DEUX variables
3. ✅ Quand ESP32 redemande l'état → GPIO 15 = 1
4. ✅ Lumière reste allumée

## 📦 Déploiement

### Compilation et upload

```bash
# Compiler pour wroom-test
pio run -e wroom-test -t upload

# Ou pour wroom-prod
pio run -e wroom-prod -t upload
```

### Test

1. Allumer la lumière via l'interface web
2. Attendre 10-15 secondes
3. Vérifier que la lumière reste allumée
4. Vérifier les logs : `[Auto] ✅ Synchronisation serveur réussie - lumière activée manuellement (local)`

## 📊 Logs de diagnostic

### Avant le fix

```
[Auto] Lumière ON (GPIO numérique)
[Auto] ✅ Synchronisation serveur réussie - lumière activée manuellement (local)
... quelques secondes plus tard ...
[Auto] Lumière OFF (GPIO numérique)  ← Le serveur renvoie GPIO 15=0
```

### Après le fix

```
[Auto] Lumière ON (GPIO numérique)
[Auto] ✅ Synchronisation serveur réussie - lumière activée manuellement (local)
... lumière reste allumée ...
```

## 🔧 Alternative (si le problème persiste)

Si la lumière s'éteint toujours après cette modification, vérifier côté serveur PHP :

1. **Fichier:** `ffp3/ffp3datas/post-ffp3-data2.php` (environnement TEST)
2. **Vérifier:** Que le script sauvegarde bien le paramètre `15=1` dans la BDD
3. **Vérifier:** Que le script `post-ffp3-data2.php` traite les paramètres GET/POST correctement

## 📅 Date de création

7 octobre 2025

## ✅ Statut

- [x] Problème diagnostiqué
- [x] Solution implémentée
- [ ] Testé sur wroom-test
- [ ] Testé sur wroom-prod
- [ ] Déployé en production

---

**Note:** Cette modification fait suite au fix mémoire PANIC qui a été un succès total (heap minimum passé de 15 500 bytes à 62 212 bytes, aucun PANIC depuis le déploiement).


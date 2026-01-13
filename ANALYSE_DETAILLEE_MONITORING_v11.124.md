# 🔍 Analyse Détaillée du Monitoring - v11.124

**Date d'analyse :** 12 janvier 2026  
**Fichier analysé :** `monitor_until_crash_2026-01-12_15-47-43.log`  
**Version firmware :** v11.124 (avant corrections v11.125)  
**Durée du monitoring :** 62.1 secondes  
**Total lignes :** 2995

---

## 📊 Résumé Exécutif

**PROBLÈME CRITIQUE IDENTIFIÉ :** Le système a effectué **6 redémarrages consécutifs** (reboot #124 à #129) en seulement 62 secondes, avec un **heap minimum critique de 52388 bytes (51 KB)** qui chute jusqu'à **30 KB** lors des opérations HTTPS.

---

## 🔴 Problème Principal : Redémarrages en Boucle

### Observations Clés

```
Redémarrages détectés :
- Reboot #124 → #125 → #126 → #127 → #128 → #129
- Temps entre redémarrages : ~10-15 secondes
- Heap minimum constant : 52388 bytes (51 KB)
- Heap minimum historique : 30824 bytes (30 KB) ⚠️ CRITIQUE
```

### Analyse des Redémarrages

**Pattern observé :**
1. Système démarre normalement
2. Heap initial : ~165 KB
3. Requête HTTPS consomme ~43 KB (de 96 KB à 53 KB)
4. Heap chute à ~30-40 KB
5. Redémarrage après ~10-15 secondes
6. Cycle se répète

**Cause probable :** Les requêtes HTTPS consomment trop de mémoire lorsque le heap est déjà faible, provoquant un crash silencieux ou un watchdog timeout.

---

## 💾 Analyse de la Mémoire (HEAP)

### Évolution du Heap

| Événement | Heap Avant | Heap Après | Consommation |
|-----------|------------|------------|--------------|
| Démarrage système | 237 KB | - | - |
| Avant requête HTTPS #1 | 165 KB | - | - |
| Après TLS handshake #1 | 165 KB | 122 KB | **-43 KB** |
| Avant requête HTTPS #2 | 134 KB | - | - |
| Après TLS handshake #2 | 134 KB | 91 KB | **-43 KB** |
| Avant requête HTTPS #3 | 89 KB | - | - |
| Après TLS handshake #3 | 89 KB | 44 KB | **-45 KB** |
| **Heap minimum historique** | - | **30 KB** | ⚠️ **CRITIQUE** |

### Points Critiques

1. **Heap minimum historique : 30 KB** (ligne 880)
   - Seuil critique : < 50 KB
   - **Le système est en dessous du seuil critique !**

2. **Consommation HTTPS constante : ~43 KB**
   - Chaque requête HTTPS consomme ~43 KB
   - Avec un heap de 89 KB, il reste seulement 44 KB après TLS
   - **Risque de crash si une autre allocation est nécessaire**

3. **Heap minimum constant : 52388 bytes (51 KB)**
   - Indique une fuite mémoire ou fragmentation
   - Le heap ne remonte jamais au-dessus de 52 KB

---

## 🌐 Analyse des Requêtes HTTPS

### Requêtes Analysées

**Requête #1 (ligne 234-283) :**
- Heap avant : 165352 bytes (161 KB)
- Heap après TLS : 122440 bytes (119 KB)
- Consommation : **-42912 bytes (-42 KB)**
- Durée : 1745 ms
- ✅ Succès

**Requête #2 (ligne 427-466) :**
- Heap avant : 134252 bytes (131 KB)
- Heap après TLS : 91888 bytes (89 KB)
- Consommation : **-42364 bytes (-41 KB)**
- Durée : ~1300 ms
- ✅ Succès

**Requête #3 (ligne 676-743) :**
- Heap avant : 89456 bytes (87 KB)
- Heap après TLS : 44704 bytes (43 KB)
- Consommation : **-44752 bytes (-44 KB)**
- Durée : ~1000 ms
- ✅ Succès mais **heap très faible après (39 KB)**

**Requête #4 (ligne 940-997) :**
- Heap avant : 97080 bytes (94 KB)
- Heap après TLS : 54964 bytes (53 KB)
- Consommation : **-42116 bytes (-41 KB)**
- Durée : ~1000 ms
- ✅ Succès

### Problème Identifié

**Les requêtes HTTPS consomment systématiquement ~43 KB**, ce qui est problématique quand :
- Le heap disponible est < 70 KB
- Plusieurs requêtes s'enchaînent rapidement
- D'autres allocations sont nécessaires (JSON, buffers, etc.)

---

## 🔍 Analyse des Redémarrages

### Redémarrages Détectés

| Reboot # | Ligne | Heap Min | Temps depuis début |
|----------|-------|----------|-------------------|
| #124 | 360 | 52388 bytes | ~7 secondes |
| #125 | 631 | 52388 bytes | ~15 secondes |
| #126 | 1138 | 52388 bytes | ~23 secondes |
| #127 | 1238 | 52388 bytes | ~26 secondes |
| #128 | 1336 | 52388 bytes | ~29 secondes |
| #129 | 1437 | 52388 bytes | ~32 secondes |

### Pattern de Redémarrage

Chaque redémarrage suit ce pattern :
```
1. Boot (rst:0x1 POWERON_RESET)
2. Initialisation système (~2-3 secondes)
3. Connexion WiFi (~2 secondes)
4. Requêtes HTTPS (~1-2 secondes chacune)
5. Heap chute à ~30-40 KB
6. Redémarrage après ~10-15 secondes
```

**Hypothèse :** Le watchdog timeout (60 secondes dans v11.124) n'est probablement pas la cause directe, car les redémarrages se produisent toutes les 10-15 secondes. Plus probablement :
- Crash mémoire (heap insuffisant)
- Fragmentation mémoire excessive
- Allocation échouée silencieusement

---

## ⚠️ Problèmes Critiques Identifiés

### 1. Heap Minimum Trop Faible ⚠️ CRITIQUE

**Problème :**
- Heap minimum historique : **30 KB**
- Seuil critique : **< 50 KB**
- Le système fonctionne en dessous du seuil critique

**Impact :**
- Risque de crash lors d'allocations
- Fragmentation mémoire élevée
- Allocations échouent silencieusement

### 2. Consommation HTTPS Excessive ⚠️ CRITIQUE

**Problème :**
- Chaque requête HTTPS consomme **~43 KB**
- Avec un heap de 87 KB, il reste seulement 44 KB après TLS
- Pas de guard mémoire avant les requêtes

**Impact :**
- Crash si heap < 70 KB avant requête
- Impossible de faire plusieurs requêtes enchaînées
- Système instable

### 3. Redémarrages en Boucle ⚠️ CRITIQUE

**Problème :**
- 6 redémarrages en 62 secondes
- Aucun log de cause explicite
- Pattern répétitif

**Impact :**
- Système inutilisable
- Perte de données
- Instabilité totale

### 4. Pas de Logs de Reset Reason ⚠️ ATTENTION

**Problème :**
- Version v11.124 n'affiche pas la cause du redémarrage
- Impossible de savoir si c'est watchdog, panic, ou autre

**Impact :**
- Diagnostic difficile
- Impossible d'identifier la cause exacte

---

## ✅ Corrections Appliquées dans v11.125

### 1. Timeout Watchdog Augmenté ✅

**Avant (v11.124) :** 60 secondes  
**Après (v11.125) :** 120 secondes (2 minutes)

**Justification :** Les opérations HTTPS peuvent prendre 5-10 secondes, et plusieurs opérations enchaînées peuvent expirer le watchdog.

### 2. Logs de Reset Reason ✅

**Ajout :** Affichage de la cause du redémarrage au démarrage

**Bénéfice :** Permet d'identifier si les redémarrages sont dus à :
- Watchdog timeout
- PANIC
- Brownout
- Autre cause

### 3. Guards Mémoire Avant HTTPS ✅

**Ajout :** Vérification que heap ≥ 70 KB avant chaque requête HTTPS

**Bénéfice :** 
- Évite les crashes si heap insuffisant
- Reporte la requête plutôt que de crasher
- Système plus stable

---

## 📋 Recommandations Supplémentaires

### Priorité 1 : Diagnostic Immédiat (URGENT)

1. **Relancer le monitoring avec v11.125**
   - Les nouveaux logs de reset reason permettront d'identifier la cause exacte
   - Les guards mémoire devraient éviter les crashes liés à HTTPS

2. **Vérifier les logs de reset reason**
   - Identifier si les redémarrages sont dus à watchdog, panic, ou autre
   - Analyser les patterns de redémarrage

### Priorité 2 : Optimisation Mémoire (CRITIQUE)

1. **Réduire la consommation HTTPS**
   - Optimiser les buffers TLS
   - Réutiliser les connexions si possible
   - Réduire la taille des certificats

2. **Identifier les fuites mémoire**
   - Monitorer le heap en continu
   - Vérifier les allocations non libérées
   - Analyser la fragmentation

3. **Augmenter le heap disponible**
   - Réduire les buffers statiques
   - Optimiser les structures de données
   - Utiliser PSRAM si disponible

### Priorité 3 : Monitoring Amélioré

1. **Alertes si heap < 60 KB**
   - Logs d'alerte automatiques
   - Report des opérations critiques

2. **Métriques de mémoire en continu**
   - Heap minimum/maximum/moyen
   - Fragmentation
   - Taille des plus gros blocs libres

---

## 🎯 Conclusion

Le fichier de monitoring révèle un **problème critique de mémoire** :
- Heap minimum historique de **30 KB** (en dessous du seuil critique de 50 KB)
- Consommation HTTPS de **~43 KB** par requête
- **6 redémarrages consécutifs** en 62 secondes

Les corrections appliquées dans **v11.125** devraient améliorer la situation :
- ✅ Timeout watchdog augmenté (120s)
- ✅ Logs de reset reason pour diagnostic
- ✅ Guards mémoire avant HTTPS

**Action requise :** Relancer le monitoring avec v11.125 pour vérifier l'efficacité des corrections et identifier la cause exacte des redémarrages grâce aux nouveaux logs.

---

**Note :** Cette analyse est basée sur un monitoring de 62 secondes avec la version v11.124. Un monitoring plus long avec v11.125 permettra de confirmer l'efficacité des corrections et d'identifier d'éventuels problèmes résiduels.

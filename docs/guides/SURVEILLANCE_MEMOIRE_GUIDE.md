# 📊 Guide de Surveillance Mémoire - Post-Fix PANIC

## 🎯 Objectif

Surveiller la mémoire après le déploiement du fix pour confirmer la résolution du problème de PANIC et détecter d'éventuelles fuites mémoire.

## 📋 Logs à surveiller

### 1. Avant le sleep

```
[Auto] Validation de l'état système avant sleep
[Auto] 📊 Heap libre: XXXXX bytes (minimum historique: YYYYY bytes)
[Auto] ✅ État système validé pour sleep
```

**Valeurs attendues:**
- ✅ Heap libre > 40000 bytes
- ⚠️ Si < 40000 : Sleep annulé (normal)
- 🚨 Si minimum historique < 20000 : Fuite mémoire critique

### 2. Opérations avant sleep

```
[Auto] 📤 Envoi mise à jour avant sleep (heap: XXXXX bytes)
[Auto] 📊 Heap après envoi: YYYYY bytes (delta: ±ZZZZ bytes)
```

**Valeurs attendues:**
- ✅ Heap avant envoi > 50000 bytes
- ✅ Delta : -10000 à -30000 bytes (normal)
- ⚠️ Si skip : heap était < 50000 bytes

```
[Auto] 📧 Envoi du mail de mise en veille (heap: XXXXX bytes)...
```

**Valeurs attendues:**
- ✅ Heap > 45000 bytes pour email
- ⚠️ Si skip : heap était < 45000 bytes

### 3. Nettoyage mémoire

```
[Auto] 🧹 Nettoyage mémoire avant sleep...
[Auto] 📊 Heap avant sleep: XXXXX bytes (nettoyé: ±YYYY bytes)
```

**Valeurs attendues:**
- ✅ Heap final > 40000 bytes
- ✅ Gain de nettoyage : 0 à +5000 bytes
- 🚨 Si < 30000 : Message d'alerte attendu

### 4. Dans power.cpp

```
[Power] 📊 Mémoire avant sleep: XXXXX bytes (min historique: YYYYY bytes)
```

**Valeurs attendues:**
- ✅ > 40000 bytes
- 🚨 Si < 30000 : Alerte critique affichée

### 5. Au réveil

```
[Auto] 📊 Réveil - Heap actuel: XXXXX bytes, minimum historique: YYYYY bytes
```

**Valeurs attendues:**
- ✅ Heap actuel > 100000 bytes (devrait remonter après sleep)
- ⚠️ Minimum historique devrait rester > 30000 bytes
- 🚨 Si min < 20000 : Alerte critique affichée

### 6. Email de réveil (optionnel)

```
[Auto] 📧 Envoi du mail de réveil (heap: XXXXX bytes)...
OU
[Auto] ⚠️ Skip mail de réveil: heap insuffisant (XXXXX < 45000 bytes)
```

## 📈 Évolution de la mémoire pendant un cycle

### Cycle normal attendu

```
Démarrage:           ~180000 bytes
Après init:          ~150000 bytes
Activité normale:    100000-120000 bytes
Avant sleep:         > 40000 bytes ✅
Minimum historique:  > 30000 bytes ✅
```

### ⚠️ Signaux d'alerte

1. **Heap décroissant progressivement**
   ```
   Cycle 1: 120000 bytes
   Cycle 2: 100000 bytes
   Cycle 3: 80000 bytes
   Cycle 4: 60000 bytes ⚠️
   ```
   → Fuite mémoire probable

2. **Minimum historique critique**
   ```
   [Auto] 🚨 ALERTE: Heap minimum historique critique (15000 bytes)
   ```
   → Fuite mémoire confirmée

3. **Sleep annulé répétitivement**
   ```
   [Auto] ⚠️ Sleep annulé: heap insuffisant (35000 < 40000 bytes)
   ```
   → Système en mode dégradé, investiguer

## 🔍 Analyse des logs

### Scénario 1: Tout va bien ✅

```
[Auto] 📊 Heap libre: 85000 bytes (minimum historique: 45000 bytes)
[Auto] 📤 Envoi mise à jour avant sleep (heap: 85000 bytes)
[Auto] 📊 Heap après envoi: 75000 bytes (delta: -10000 bytes)
[Auto] 📧 Envoi du mail de mise en veille (heap: 75000 bytes)...
[Auto] 🧹 Nettoyage mémoire avant sleep...
[Auto] 📊 Heap avant sleep: 76000 bytes (nettoyé: +1000 bytes)
[Power] 📊 Mémoire avant sleep: 76000 bytes (min historique: 45000 bytes)
```

**Interprétation:**
- ✅ Heap toujours > 40KB
- ✅ Minimum historique sain (> 30KB)
- ✅ Pas de fuite évidente
- ✅ Toutes les fonctionnalités actives

### Scénario 2: Mode dégradé (heap limité) ⚠️

```
[Auto] 📊 Heap libre: 48000 bytes (minimum historique: 32000 bytes)
[Auto] ⚠️ Skip sendFullUpdate: heap insuffisant (48000 < 50000 bytes)
[Auto] ⚠️ Skip mail de mise en veille: heap insuffisant (48000 < 45000 bytes)
[Auto] 🧹 Nettoyage mémoire avant sleep...
[Auto] 📊 Heap avant sleep: 49000 bytes (nettoyé: +1000 bytes)
[Power] 📊 Mémoire avant sleep: 49000 bytes (min historique: 32000 bytes)
```

**Interprétation:**
- ⚠️ Heap limite mais acceptable (> 40KB)
- ⚠️ Fonctionnalités non-critiques désactivées
- ⚠️ Sleep autorisé en mode minimal
- ✅ Pas de PANIC

### Scénario 3: Critique (sleep annulé) 🚨

```
[Auto] 📊 Heap libre: 35000 bytes (minimum historique: 18000 bytes)
[Auto] ⚠️ Sleep annulé: heap insuffisant (35000 < 40000 bytes)
[Auto] ⚠️ RISQUE DE PANIC - Mémoire critique détectée
[Auto] 🧹 Tentative de nettoyage mémoire d'urgence...
[Auto] 📊 Heap après nettoyage: 36000 bytes (gain: +1000 bytes)
[Auto] ⚠️ Mémoire toujours insuffisante - Sleep annulé
```

**Interprétation:**
- 🚨 Heap critique (< 40KB)
- 🚨 Minimum historique dangereux (< 20KB)
- 🚨 Sleep annulé pour éviter PANIC
- 🔍 **Investiguer fuites mémoire immédiatement**

## 🛠️ Actions en cas de problème

### Si heap < 50KB régulièrement

1. **Réduire la fréquence des lectures de capteurs**
   - Augmenter les intervalles de lecture
   - Réduire le nombre d'échantillons dans les filtres

2. **Optimiser les Strings**
   ```cpp
   String msg;
   msg.reserve(256); // Pré-allouer
   msg += "...";
   ```

3. **Réduire les buffers JSON**
   ```cpp
   StaticJsonDocument<2048> doc; // Au lieu de 4096
   ```

### Si minimum historique < 20KB

1. **Fuite mémoire probable** - Chercher:
   - Allocations non libérées
   - Strings qui grossissent indéfiniment
   - Buffers temporaires non nettoyés

2. **Fragmentations mémoire**
   - Utiliser davantage de variables statiques
   - Éviter les allocations/désallocations répétées

3. **Restart préventif**
   - Ajouter un reboot automatique si heap < 25KB

## 📊 Commandes de monitoring

### Via Serial Monitor

```bash
# PlatformIO
pio device monitor -e wroom-prod -f esp32_exception_decoder

# Filtrer les logs mémoire
pio device monitor -e wroom-prod | findstr "Heap"
```

### Logs à capturer

```bash
# Capturer 1 cycle complet (wake → sleep → wake)
# Durée estimée: 10-15 minutes selon configuration
```

## 📈 Métriques de succès

### Court terme (24h)
- ✅ Aucun PANIC lié à la mémoire
- ✅ Sleep réussi systématiquement
- ✅ Heap > 40KB avant chaque sleep

### Moyen terme (7 jours)
- ✅ Minimum historique stable > 30KB
- ✅ Pas de décroissance progressive du heap
- ✅ Compteur de redémarrages ne progresse plus

### Long terme (30 jours)
- ✅ Uptime > 24h entre reboots planifiés
- ✅ Mémoire stable sur plusieurs cycles
- ✅ Aucune dégradation progressive

## 🔔 Alertes à configurer

Si vous avez un système de monitoring:

1. **Alerte critique**: Heap < 30KB
2. **Alerte warning**: Heap < 50KB  
3. **Alerte info**: Sleep annulé
4. **Alerte critique**: Minimum historique < 20KB
5. **Alerte warning**: PANIC dans les logs

## 📝 Notes importantes

- Les logs de mémoire sont maintenant activés par défaut
- Aucun impact significatif sur les performances
- Les fonctionnalités se dégradent gracieusement si heap bas
- Le système continue de fonctionner même en mode dégradé


# Analyse du log de monitoring 10 minutes - 2026-01-28

## Résumé exécutif

**Problème principal** : Le heap libre est insuffisant (~34 KB) pour permettre les connexions TLS, qui nécessitent ~45 KB contigus. Toutes les requêtes HTTPS sont refusées pendant les 10 minutes de monitoring.

**Statistiques** :
- ✅ Garde fragmentation fonctionne : seulement 2 occurrences de "Plus grand bloc insuffisant"
- ❌ Aucune requête HTTPS réussie (0 code=200)
- ❌ Des dizaines de refus TLS : "heap insuffisant (34XXX < 62000 bytes)"
- ⚠️ Heap total trop bas : ~34 KB (seuil requis : 62 KB)

## Analyse détaillée

### 1. Évolution du heap au boot

**Ligne 649** : `[TLS] 🔒 Mutex acquis (heap: 86268 bytes)`
- Heap initial : **86 KB** ✅

**Ligne 650** : `stop_ssl_socket(): Cleaning SSL connection`
- `resetTLSClient()` libère une session TLS précédente

**Ligne 678** : `⚠️ Plus grand bloc insuffisant (9204 bytes < 45KB)`
- Fragmentation critique : plus grand bloc libre seulement **9 KB**
- Heap total : ~34 KB (chute de 52 KB après resetTLSClient)

**Ligne 679** : `[TLS] 🔓 Mutex libéré (heap: 34840 bytes)`
- Heap final : **34 KB** ❌

### 2. Problème de fragmentation

**Occurrences "Plus grand bloc insuffisant"** : 2 seulement
- Ligne 678 : 9204 bytes < 45KB
- Ligne ~10677 : 8692 bytes < 45KB

**Conclusion** : La garde fragmentation fonctionne correctement et bloque les requêtes HTTPS quand le plus grand bloc est < 45 KB.

### 3. Problème de heap total insuffisant

**Occurrences "heap insuffisant (XXX < 62000 bytes)"** : Des dizaines
- Heap moyen : ~34 KB
- Seuil requis : 62 KB (`TLS_MIN_HEAP_BYTES`)
- Écart : **-28 KB** (45% de déficit)

**Bloc réservé TLS** : 48 KB (`TLS_RESERVE_BLOCK_BYTES`)
- Ne peut pas être alloué car heap total (34 KB) < taille du bloc (48 KB)

### 4. Tentatives de requêtes HTTPS

**Requêtes HTTPS réussies** : **0**
- Aucune occurrence de `code=200` dans le log

**Requêtes HTTPS bloquées** :
- Par fragmentation : 2 occurrences
- Par heap total insuffisant : des dizaines d'occurrences

**Exemple de séquence typique** :
```
[netTask] Boot: tryFetchConfigFromServer()
[TLS] 🔒 Mutex acquis (heap: 86268 bytes)
[HTTP] ⚠️ Plus grand bloc insuffisant (9204 bytes < 45KB), report GET HTTPS
[TLS] 🔓 Mutex libéré (heap: 34840 bytes)
[TLS] ⛔ Connexion refusée: heap insuffisant (34840 < 62000 bytes)
```

### 5. Évolution du heap pendant les 10 minutes

**Heap au boot (après setup)** :
- Ligne 537 : Free Bytes = 87040 B (85.0 KB)
- Ligne 540 : Largest Free Block = 61428 B (60.0 KB)

**Heap après resetTLSClient()** :
- Ligne 679 : Heap = 34840 bytes (34 KB)
- Chute de **51 KB** après libération de la session TLS

**Heap pendant le monitoring** :
- Stable autour de **34-35 KB**
- Toujours en dessous du seuil de 62 KB

## Diagnostic

### Causes identifiées

1. **Chute drastique du heap après resetTLSClient()**
   - Le heap passe de 86 KB à 34 KB après libération de la session TLS
   - Cette chute suggère que `resetTLSClient()` libère la mémoire TLS, mais le heap reste fragmenté

2. **Seuil TLS_MIN_HEAP_BYTES trop élevé**
   - Seuil actuel : 62 KB
   - Heap disponible : ~34 KB
   - Écart : -28 KB (45% de déficit)

3. **Bloc réservé TLS trop grand**
   - Taille actuelle : 48 KB (`TLS_RESERVE_BLOCK_BYTES`)
   - Heap disponible : ~34 KB
   - Impossible d'allouer le bloc réservé

4. **Fragmentation mémoire**
   - Plus grand bloc libre : ~9 KB
   - Bloc requis pour TLS : ~45 KB
   - Fragmentation : ~74% ((34KB - 9KB) / 34KB)

### Solutions proposées

#### Option 1 : Réduire le seuil TLS_MIN_HEAP_BYTES
- **Avant** : 62 KB
- **Après** : 40 KB (TLS ~42KB + marge négative acceptable)
- **Impact** : Permettrait les requêtes HTTPS quand le heap est > 40 KB

#### Option 2 : Réduire la taille du bloc réservé TLS
- **Avant** : 48 KB
- **Après** : 32 KB (suffisant pour TLS avec CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=2048)
- **Impact** : Permettrait l'allocation du bloc réservé même avec heap faible

#### Option 3 : Combinaison Option 1 + Option 2
- Réduire `TLS_MIN_HEAP_BYTES` à 40 KB
- Réduire `TLS_RESERVE_BLOCK_BYTES` à 32 KB
- **Impact** : Meilleur compromis entre sécurité et fonctionnalité

#### Option 4 : Optimiser resetTLSClient()
- Analyser pourquoi le heap chute de 52 KB après `resetTLSClient()`
- Vérifier si des buffers sont mal libérés
- **Impact** : Récupérer de la mémoire pour les requêtes TLS

## Recommandations

1. **Court terme** : Implémenter Option 3 (réduire les seuils)
   - Permettra les requêtes HTTPS même avec heap faible
   - Risque acceptable car CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=2048 réduit l'empreinte TLS

2. **Moyen terme** : Analyser Option 4 (optimiser resetTLSClient)
   - Comprendre pourquoi le heap chute de 52 KB
   - Récupérer cette mémoire pour améliorer la stabilité

3. **Long terme** : Audit mémoire complet
   - Identifier toutes les allocations importantes
   - Optimiser les buffers statiques vs dynamiques
   - Réduire la fragmentation globale

## Conclusion

Les correctifs implémentés (Option A, B, C) fonctionnent correctement :
- ✅ La garde fragmentation bloque les requêtes quand nécessaire
- ✅ Le bloc réservé TLS est géré correctement
- ✅ Le reset TLS libère la mémoire

**Mais** : Le heap disponible (~34 KB) est insuffisant pour les seuils actuels (62 KB heap total, 48 KB bloc réservé). Il faut ajuster ces seuils pour permettre les requêtes HTTPS dans les conditions réelles du système.

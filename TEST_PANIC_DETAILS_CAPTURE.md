# Test de capture des détails de PANIC

## Objectif

Vérifier que les informations détaillées de Guru Meditation sont correctement capturées, sauvegardées et rapportées dans le mail de démarrage.

## Pré-requis

- Firmware compilé avec les modifications de `diagnostics.cpp` et `diagnostics.h`
- Accès au port série pour voir les logs
- Configuration email opérationnelle pour recevoir le mail de démarrage

## Méthodes de test

### Méthode 1 : Provoquer un Task Watchdog (recommandé)

Ajoutez temporairement ce code dans `setup()` de `app.cpp` :

```cpp
void setup() {
  // ... code existant ...
  
  // TEST: Provoquer un Task Watchdog après 10 secondes
  delay(10000);
  Serial.println("TEST: Provocation d'un Task Watchdog...");
  while(true) {
    // Boucle infinie pour bloquer la tâche et déclencher le WDT
    delay(10);
  }
}
```

**Résultat attendu** : Task Watchdog après ~5 secondes de boucle infinie

### Méthode 2 : Provoquer une exception de mémoire

```cpp
void setup() {
  // ... code existant ...
  
  // TEST: Provoquer un panic par accès mémoire invalide
  delay(10000);
  Serial.println("TEST: Provocation d'un accès mémoire invalide...");
  int* ptr = nullptr;
  *ptr = 42;  // Accès à nullptr -> PANIC
}
```

**Résultat attendu** : Load/Store error avec adresse 0x00000000

### Méthode 3 : Provoquer un assert

```cpp
void setup() {
  // ... code existant ...
  
  // TEST: Provoquer un assert
  delay(10000);
  Serial.println("TEST: Provocation d'un assert...");
  assert(false);  // Force un panic
}
```

**Résultat attendu** : Assertion failed

## Vérifications

### 1. Logs série au boot suivant

Après le redémarrage causé par le panic, vérifier dans les logs série :

```
[Diagnostics] 🔍 Panic capturé: Timer Group Watchdog (CPU) (Core 0)
[Diagnostics] 💾 Informations de panic sauvegardées
[Diagnostics] 🚀 Initialisé - reboot #297, minHeap: 48524 bytes [PANIC: Timer Group Watchdog (CPU)]
[Diagnostics] 📖 Informations de panic chargées depuis NVS
```

### 2. Mail de démarrage

Le mail reçu devrait contenir une section comme :

```
[RESTART INFO] Informations de redémarrage:
Raison du redémarrage: PANIC (erreur critique)

⚠️ DÉTAILS DU PANIC (Guru Meditation) ⚠️
Exception: Timer Group Watchdog (CPU)
Adresse PC: 0x400d1234
CPU Core: 0

Compteur de redémarrages: 297
Heap libre au démarrage: 178168 bytes
Heap libre minimum historique: 48524 bytes
```

### 3. Vérification NVS

Vous pouvez aussi vérifier que les informations sont bien sauvegardées dans NVS en ajoutant ce code de debug :

```cpp
Preferences prefs;
prefs.begin("diagnostics", true);
if (prefs.getBool("hasPanic", false)) {
  Serial.println("Panic info in NVS:");
  Serial.println("  Cause: " + prefs.getString("panicCause", ""));
  Serial.println("  Core: " + String(prefs.getInt("panicCore", -1)));
}
prefs.end();
```

## Séquence normale de test

1. **Upload du firmware** avec code de test
2. **Attendre 10 secondes** (délai du test)
3. **Panic se produit** → ESP32 redémarre automatiquement
4. **Au boot suivant** :
   - Logs montrent la capture du panic
   - Informations sauvegardées dans NVS
   - Mail envoyé avec les détails
5. **Vérifier le mail** reçu pour confirmer les détails

## Résultats attendus par type de panic

### Task Watchdog (TGWDT_CPU_RESET)

```
Exception: Timer Group Watchdog (CPU)
```

### Load/Store Error

```
Exception: Software CPU Reset
```
(Note: Les exceptions mémoire sont souvent transformées en SW reset)

### RTC Watchdog

```
Exception: RTC Watchdog Reset
```

## Nettoyage après test

1. **Retirer le code de test** de `app.cpp`
2. **Recompiler et uploader** le firmware propre
3. **Vérifier** que le système fonctionne normalement

## Troubleshooting

### Les informations ne sont pas dans le mail

**Causes possibles** :
- Les infos de panic ont été nettoyées avant l'envoi du mail
- Le panic n'a pas été détecté (vérifier les logs série)
- Le mail a été envoyé trop rapidement (avant la sauvegarde NVS)

**Solution** : Vérifier l'ordre d'appel dans `app.cpp` :
1. `diag.begin()` doit être appelé en premier
2. Ensuite le reste de l'initialisation
3. Enfin l'envoi du mail de démarrage

### Logs montrent "Unknown Exception"

C'est normal pour certains types de reset moins courants. Le code numérique est inclus dans `additionalInfo` pour aide au diagnostic.

### Le système ne redémarre pas après le panic

Certains panics peuvent nécessiter un reset matériel. Utilisez le bouton RESET ou coupez l'alimentation.

## Exemples de résultats réels en production

### Exemple 1 : Watchdog de mise en veille

```
⚠️ DÉTAILS DU PANIC (Guru Meditation) ⚠️
Exception: Timer Group Watchdog (CPU)
CPU Core: 0
```

Diagnostic : La fonction de mise en veille a pris trop de temps (> 5s)

### Exemple 2 : Problème mémoire

```
⚠️ DÉTAILS DU PANIC (Guru Meditation) ⚠️
Exception: Software CPU Reset
Adresse mémoire fautive: 0x3ffb2000
```

Diagnostic : Accès à une zone mémoire non valide ou libérée

### Exemple 3 : Multiple panics

```
⚠️ DÉTAILS DU PANIC (Guru Meditation) ⚠️
Exception: RTC Watchdog CPU Reset
CPU Core: 0
Info supplémentaire: Core 1 reason differs: 3
```

Diagnostic : Les deux cores ont paniqué pour des raisons différentes

## Notes importantes

1. **Compteur de redémarrages élevé** (> 100) peut indiquer un problème récurrent
2. **Heap minimum faible** (< 30 KB) peut causer des panics
3. **Différence entre cores** indique un problème de synchronisation
4. Les informations de panic sont **automatiquement nettoyées** après lecture pour ne pas polluer le prochain boot

## Informations complémentaires

Pour obtenir encore plus de détails (backtrace complète), il faut activer le Core Dump dans `platformio.ini` :

```ini
build_flags =
    -DCONFIG_ESP32_ENABLE_COREDUMP=1
    -DCONFIG_ESP32_ENABLE_COREDUMP_TO_FLASH=1
```

Et ajouter une partition coredump dans le fichier de partitions.

---

**Auteur** : Assistant IA  
**Date** : 2025-10-08  
**Version** : 1.0


# Interprétation du PANIC lors de la mise en veille

## Votre cas spécifique

**Symptôme** : PANIC juste après la réception du mail de mise en veille

```
Raison du redémarrage: PANIC (erreur critique)
Compteur de redémarrages: 296  ← Élevé !
Heap libre minimum historique: 50548 bytes  ← Limite basse
```

## Avec les nouvelles informations, vous saurez

### Scénario 1 : Timer Group Watchdog (CPU)

```
Exception: Timer Group Watchdog (CPU)
CPU Core: 0
```

**Signification** : La tâche qui gère la mise en veille a pris plus de 5 secondes sans rendre la main au watchdog.

**Causes probables** :
- Opération bloquante dans le processus de mise en veille
- Attente WiFi/SMTP trop longue
- Opération NVS/LittleFS qui bloque
- Boucle infinie accidentelle

**Solutions** :
- Ajouter des `vTaskDelay()` dans les longues opérations
- Utiliser des opérations non-bloquantes
- Augmenter le timeout du watchdog (déconseillé)
- Fragmenter l'opération en étapes plus courtes

### Scénario 2 : RTC Watchdog

```
Exception: RTC Watchdog Reset
CPU Core: 0
```

**Signification** : Le système tout entier était bloqué (interruptions désactivées trop longtemps).

**Causes probables** :
- Section critique trop longue (`portENTER_CRITICAL`)
- Flash write bloquant pendant > 300ms
- Opération DMA mal gérée

**Solutions** :
- Réduire la taille des sections critiques
- Utiliser des buffers pour les écritures flash
- Vérifier les opérations DMA

### Scénario 3 : Software CPU Reset

```
Exception: Software CPU Reset
Adresse mémoire fautive: 0x3ffb2000
```

**Signification** : Exception mémoire (accès invalide, stack overflow).

**Causes probables** :
- Stack overflow dans une tâche
- Pointeur invalide (freed memory, nullptr)
- Corruption de heap (heap libre = 50 KB = très bas!)

**Solutions** :
- Augmenter la taille de stack des tâches
- Vérifier les allocations/libérations mémoire
- Investiguer pourquoi le heap minimum est si bas

### Scénario 4 : Multiple Cores affectés

```
Exception: Timer Group Watchdog (CPU)
CPU Core: 0
Info supplémentaire: Core 1 reason differs: 3
```

**Signification** : Problème de synchronisation entre les deux cores.

**Causes probables** :
- Deadlock sur un mutex ou sémaphore
- Problème dans une opération multi-core (WiFi, Bluetooth)
- Race condition

**Solutions** :
- Revoir la synchronisation entre tâches
- Éviter les mutex dans les interruptions
- Utiliser des queues au lieu de mutex quand possible

## Analyse spécifique de votre situation

### Indicateurs importants

1. **Compteur de redémarrages: 296**
   - C'est très élevé !
   - Suggère un problème récurrent
   - Probablement pas un cas isolé

2. **Heap minimum: 50548 bytes**
   - Pour un ESP32 WROOM-32 (320 KB RAM), c'est bas
   - Risque élevé de fragmentation
   - Peut causer des panics lors d'allocations

3. **Timing: "juste après le mail de mise en veille"**
   - Le système est encore en activité WiFi
   - Les buffers SMTP/TCP sont potentiellement encore alloués
   - La mémoire est au plus haut usage

### Hypothèse principale

**Le plus probable** : Timer Group Watchdog pendant l'opération de mise en veille

**Pourquoi** :
1. Mail envoyé (opération longue avec SMTP/TLS)
2. WiFi encore actif
3. Mémoire presque saturée (50 KB libre)
4. Tentative de mise en veille qui échoue ou prend trop de temps
5. Watchdog déclenché car la tâche ne répond plus

### Ce qu'il faut chercher dans le prochain mail

Si vous voyez :
- **Timer Group Watchdog** → Le processus de veille est trop long
- **Software Reset + Adresse mémoire** → Problème d'allocation mémoire
- **RTC Watchdog** → Section critique bloquante

## Actions recommandées

### Immédiat

1. **Déployer le nouveau code** avec capture de panic
2. **Attendre le prochain panic** (vu le compteur à 296, ça va arriver)
3. **Analyser le mail** avec les détails du Guru Meditation

### Si Timer Group Watchdog

```cpp
// Dans power.cpp ou le fichier qui gère la mise en veille
void goToSleep() {
  // Ajouter des délais pour permettre au watchdog de respirer
  vTaskDelay(pdMS_TO_TICKS(10));  // À chaque étape
  
  // Ou augmenter temporairement le timeout du watchdog
  esp_task_wdt_init(10, false);  // 10 secondes au lieu de 5
}
```

### Si Software Reset (mémoire)

Vérifier la séquence de nettoyage :

```cpp
void goToSleep() {
  // 1. Libérer les buffers SMTP/TCP
  mailer.cleanup();
  vTaskDelay(100);
  
  // 2. Déconnecter WiFi proprement
  WiFi.disconnect(true);
  vTaskDelay(500);
  
  // 3. Afficher la mémoire disponible
  Serial.printf("Heap avant veille: %u bytes\n", ESP.getFreeHeap());
  
  // 4. Seulement alors, mise en veille
  esp_light_sleep_start();
}
```

## Monitoring continu

Avec la nouvelle fonctionnalité, chaque redémarrage vous donnera :

1. **Le type exact** d'exception
2. **Le contexte** (core, adresse, tâche)
3. **L'historique** mémoire

Cela vous permettra de :
- Identifier si c'est toujours le même type de panic
- Voir si les changements réduisent la fréquence
- Détecter des patterns (ex: panic uniquement quand heap < 60 KB)

## Checklist de diagnostic

Après le prochain PANIC, vérifier :

- [ ] Type d'exception reçu dans le mail
- [ ] Core CPU concerné
- [ ] Heap libre au moment du panic
- [ ] Correspondance avec le log série (si disponible)
- [ ] Compteur de redémarrages a augmenté de combien depuis le dernier mail
- [ ] Y a-t-il une information supplémentaire (additionalInfo)

## Conclusion

Le prochain mail de démarrage vous dira **exactement** ce qui se passe. En attendant, préparez-vous à :

1. **Recevoir le mail détaillé** après le prochain panic
2. **Analyser le type d'exception** (probablement Timer Group Watchdog)
3. **Appliquer les corrections** en fonction du type identifié
4. **Monitorer** si le problème est résolu (compteur de redémarrages)

---

**Note** : Avec 296 redémarrages, le prochain panic ne devrait pas tarder. Les nouvelles informations vous permettront de diagnostiquer précisément le problème au lieu de deviner.

**Date** : 2025-10-08  
**Auteur** : Assistant IA


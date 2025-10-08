# Guide de Diagnostic ESP32 - Identification des Redémarrages

## 🚨 Problème : ESP32 qui redémarre de manière inattendue

Ce guide vous aide à identifier la cause des redémarrages de votre ESP32 en utilisant les outils de diagnostic intégrés.

## 🔧 Outils de Diagnostic Ajoutés

- Timeout configuré à 5 minutes
- Réinitialisation automatique dans la boucle principale
- Détection des blocages de code

### 2. **Surveillance de la Mémoire**
- Vérification de la heap libre toutes les 10 secondes
- Détection de la fragmentation mémoire
- Alertes si moins de 10KB disponibles

### 3. **Surveillance de la Stack**
- Vérification de l'espace stack libre toutes les 30 secondes
- Détection des débordements de stack

### 4. **Tracé des Erreurs**
- Logging automatique des erreurs critiques
- Sauvegarde en mémoire flash
- Historique des redémarrages

### 5. **Diagnostic JSON/HTTP**
- Tracé détaillé des requêtes HTTP
- Validation des réponses JSON
- Gestion sécurisée des erreurs

## 📊 Comment Utiliser les Outils

### Étape 1 : Compiler et Flasher
```bash
pio run -t upload
```

### Étape 2 : Surveiller les Logs
Connectez-vous au port série et observez les messages de diagnostic :

```
🔍 Démarrage avec diagnostic activé
🔄 Redémarrage #1 - Raison: PANIC
📊 Mem: Free=123456, Min=123456, MaxBlock=123456
📊 Stack: Free=1234 bytes
🔍 dataJSONtoesp() appelée
🔍 Réponse HTTP reçue, longueur: 1024
✅ Parsing JSON réussi
```

### Étape 3 : Analyser avec l'Outillage Python

#### Mode Temps Réel :
```bash
python diagnostic_analyzer.py
```

#### Mode Fichier de Log :
```bash
python diagnostic_analyzer.py esp32_log.txt
```

## 🔍 Types de Redémarrages et Causes

### 1. **ESP_RST_PANIC** - Panic Critique
**Symptômes :**
- Messages "Guru Meditation Error"
- Accès mémoire invalide
- Division par zéro

**Causes possibles :**
- Accès à un pointeur NULL
- Débordement de buffer
- Corruption mémoire

**Solutions :**
- Vérifier les accès aux tableaux
- Valider les pointeurs avant utilisation
- Utiliser des buffers plus grands

### 2. **ESP_RST_TASK_WDT** - Watchdog Task
**Symptômes :**
- Fonction `loop()` trop lente
- Blocage dans une fonction

**Causes possibles :**
- `delay()` trop long
- Boucles infinies
- Attente réseau bloquante

**Solutions :**
- Remplacer `delay()` par `millis()`
- Ajouter des timeouts
- Utiliser des tâches FreeRTOS

### 3. **ESP_RST_INT_WDT** - Watchdog Interrupt
**Symptômes :**
- Interruptions trop longues
- Désactivation des interruptions

**Causes possibles :**
- ISR trop complexe
- Désactivation prolongée des interruptions

**Solutions :**
- Simplifier les ISR
- Limiter le temps en interruption

### 4. **ESP_RST_BROWNOUT** - Problème d'Alimentation
**Symptômes :**
- Tension d'alimentation insuffisante
- Consommation électrique excessive

**Causes possibles :**
- Alimentation sous-dimensionnée
- Court-circuit
- Composants défaillants

**Solutions :**
- Vérifier l'alimentation
- Mesurer la consommation
- Ajouter des condensateurs

### 5. **ESP_RST_SW** - Redémarrage Logiciel
**Symptômes :**
- Appel à `ESP.restart()`
- Redémarrage propre

**Causes possibles :**
- Code de mise à jour
- Gestion d'erreur
- Mode de récupération

## 🛠️ Actions Correctives par Type de Problème

### Problèmes de Mémoire
```cpp
// Avant (problématique)
String response = httpRequest(url);
JSONVar obj = JSON.parse(response);

// Après (sécurisé)
String response = httpRequest(url);
if (response.length() > 0) {
    JSONVar obj = JSON.parse(response);
    if (JSON.typeof(obj) != "undefined") {
        // Traitement sécurisé
    }
}
```

### Problèmes de Watchdog
```cpp
// Avant (bloquant)
void loop() {
    delay(5000); // Bloque 5 secondes
    // Code...
}

// Après (non-bloquant)
unsigned long lastTime = 0;
void loop() {
    if (millis() - lastTime > 5000) {
        lastTime = millis();
        // Code...
    }
}
```

### Problèmes JSON
```cpp
// Avant (non-sécurisé)
String value = myObject[keys[7]];

// Après (sécurisé)
if (myObject.hasOwnProperty("fieldName")) {
    String value = (const char*)myObject["fieldName"];
}
```

## 📈 Interprétation des Statistiques

### Mémoire
- **Heap libre > 50KB** : Normal
- **Heap libre 10-50KB** : Attention
- **Heap libre < 10KB** : Critique

### Stack
- **Stack libre > 2KB** : Normal
- **Stack libre 1-2KB** : Attention
- **Stack libre < 1KB** : Critique

### Fréquence des Redémarrages
- **< 1 par heure** : Acceptable
- **1-5 par heure** : Problématique
- **> 5 par heure** : Critique

## 🔧 Optimisations Recommandées

### 1. **Gestion de la Mémoire**
```cpp
// Utiliser des buffers statiques
char buffer[1024] = {0};
// Au lieu de
String dynamicString;
```

### 2. **Optimisation des Boucles**
```cpp
// Éviter les boucles longues
for (int i = 0; i < 1000; i++) {
    delay(100); // 100 secondes total !
}

// Préférer
unsigned long startTime = millis();
while (millis() - startTime < 10000) { // 10 secondes max
    // Code avec yield()
    yield();
}
```

### 3. **Gestion des Erreurs**
```cpp
// Toujours vérifier les retours
if (WiFi.status() != WL_CONNECTED) {
    logError("WiFi déconnecté");
    return;
}
```

## 📋 Checklist de Diagnostic

- [ ] Compiler avec les outils de diagnostic
- [ ] Surveiller les logs série
- [ ] Identifier le type de redémarrage
- [ ] Analyser les patterns d'erreur
- [ ] Vérifier la consommation mémoire
- [ ] Tester l'alimentation
- [ ] Appliquer les corrections appropriées
- [ ] Re-tester après correction

## 🆘 En Cas de Problème Persistant

1. **Augmenter la verbosité** : Ajouter plus de logs
2. **Isoler le problème** : Commenter des sections de code
3. **Tester matériel** : Vérifier l'alimentation et les connexions
4. **Consulter la documentation** : ESP32 Technical Reference Manual
5. **Demander de l'aide** : Forums ESP32, GitHub Issues

## 📞 Support

En cas de problème persistant, fournissez :
- Les logs de diagnostic complets
- Le type de redémarrage détecté
- Les actions déjà tentées
- La configuration matérielle

---

*Ce guide est basé sur l'expérience avec les ESP32 et les problèmes courants rencontrés.* 

• Le message est émis par le TWDT (Task-WatchDog Timer) quand on appelle  
  `esp_task_wdt_reset()` pour une tâche qui n’a **pas** été inscrite au TWDT.

Dans votre code :

1. Vous avez supprimé `setupWatchdog()` (qui faisait `esp_task_wdt_add(NULL)`),  
2. La fonction `resetWatchdog()` est toujours appelée dans `loop()`.  
   Or la tâche `loopTask` n’a plus été enregistrée auprès du TWDT.

Résultat : à chaque appel, l’API répond “task not found” et logue l’erreur.

Correction recommandée (Arduino + ESP-IDF) :

```cpp
#include <esp_task_wdt.h>

void setup() {
    // Initialiser le watchdog tâche à 5 minutes (300 s), pas de panic automatique
    esp_task_wdt_init(300, false);
    // Enregistrer la tâche courante (loopTask) auprès du TWDT
    esp_task_wdt_add(NULL);
}

void loop() {
    // Votre code...
    esp_task_wdt_reset();
}
```

Ainsi :

• Le message “task not found” disparaît.  
• Le watchdog continuera de surveiller la boucle principale. 
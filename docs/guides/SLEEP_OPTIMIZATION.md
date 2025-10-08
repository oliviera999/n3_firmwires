# Optimisations du système de Light Sleep

## Vue d'ensemble

Ce document décrit les améliorations apportées au système de light sleep de l'ESP32-S3 pour optimiser la consommation énergétique et améliorer la fiabilité.

## Nouvelles fonctionnalités implémentées

### 1. **Détection d'activité fine**

#### **Principe**
Au lieu de se baser uniquement sur l'état de la pompe de remplissage, le système détecte maintenant toute activité significative qui justifie de différer le sleep.

#### **Types d'activité détectés**
- Désactivé dans la version actuelle: la décision de sleep ne se base plus
  sur ces activités; seuls les cas explicitement bloquants sont considérés
  (pompe réservoir active, nourrissage en cours, décompte actif).

#### **Code implémenté**
```cpp
bool Automatism::hasSignificantActivity() {
  // Désactivé: on ne retarde plus le sleep sur l'activité capteurs/web
  // ni sur les autres actionneurs. Les cas bloquants sont gérés ailleurs.
  return false;
}
```

### 2. **Sleep adaptatif**

#### **Principe**
Le délai de sleep s'adapte automatiquement selon les conditions du système et l'historique des réveils.

#### **Facteurs d'adaptation**
- **Erreurs récentes** : Délai réduit à 5 minutes pour surveillance accrue
- **Période nocturne** : Délai augmenté à 30 minutes (22h-6h)
- **Échecs de réveil** : Délai doublé en cas d'échecs consécutifs
- **Limites min/max** : Entre 5 minutes et 1 heure

#### **Configuration**
```cpp
struct SleepConfig {
  uint32_t minSleepTime = 300;      // 5 min minimum
  uint32_t maxSleepTime = 3600;     // 1h maximum
  uint32_t normalSleepTime = 600;   // 10 min normal
  uint32_t errorSleepTime = 300;    // 5 min si erreurs
  uint32_t nightSleepTime = 1800;   // 30 min la nuit
  bool adaptiveSleep = true;        // sleep adaptatif activé
};
```

### 3. **Validation de l'état système**

#### **Avant le sleep**
Le système vérifie minimalement que l'état est compatible avec un sleep :
- Aucun actionneur critique actif (pompe réservoir)

#### **Après le réveil**
Le système vérifie que tout fonctionne correctement :
- Réactivité des capteurs
- Connexion WiFi
- Niveaux d'eau normaux

#### **Code de validation**
```cpp
bool Automatism::validateSystemStateBeforeSleep() {
  // Vérifier qu'aucun actionneur critique n'est actif
  if (_acts.isTankPumpRunning()) return false;
  return true;
}
```

### 4. **Détection d'anomalies**

#### **Types d'anomalies détectées**
- **Sleep interrompu prématurément** : Durée < 80% de la durée attendue
- **Échecs de réveil consécutifs** : Plus de 3 échecs consécutifs
- **Échecs de reconnexion WiFi** : Problèmes de connectivité

#### **Gestion des anomalies**
- Marquage des erreurs récentes
- Ajustement automatique des délais de sleep
- Logs détaillés pour le débogage

### 5. **Logging d'activité**

#### **Nouveau système de logs**
```cpp
void Automatism::logActivity(const char* activity) {
  Serial.printf("[Auto] Activité détectée: %s\n", activity);
  updateActivityTimestamp();
}
```

#### **Activités loggées**
- Démarrage/arrêt des pompes
- Changements d'état critiques
- Activité web
- Erreurs système

## Avantages des améliorations

### 1. **Optimisation énergétique**
- **Sleep plus intelligent** : Délais adaptés aux conditions
- **Moins de réveils inutiles** : Détection d'activité fine
- **Sleep nocturne prolongé** : Économie d'énergie la nuit

### 2. **Fiabilité améliorée**
- **Validation pré-sleep** : Évite les sleep dans des états critiques
- **Vérification post-réveil** : Détection rapide des problèmes
- **Gestion des erreurs** : Adaptation automatique aux problèmes

### 3. **Maintenance facilitée**
- **Logs détaillés** : Traçabilité complète des activités
- **Détection d'anomalies** : Alertes précoces
- **Diagnostic automatisé** : Identification des problèmes

## Configuration et personnalisation

### **Activation/désactivation**
```cpp
// Désactiver le sleep adaptatif
_sleepConfig.adaptiveSleep = false;

// Modifier les délais
_sleepConfig.normalSleepTime = 900;  // 15 minutes
_sleepConfig.nightSleepTime = 2400;  // 40 minutes
```

### **Seuils personnalisables**
- Délais d'activité des capteurs (actuellement 2 min)
- Délais d'activité web (actuellement 30 s)
- Limites de température et niveaux d'eau
- Nombre d'échecs consécutifs tolérés

## Monitoring et débogage

### **Logs à surveiller**
```
[Auto] Activité détectée: Démarrage pompe réservoir automatique
[Auto] Sleep adaptatif: délai réduit (erreurs récentes)
[Auto] Sleep adaptatif: délai augmenté (nuit)
[Auto] ✅ État système validé pour sleep
[Auto] ⚠️ Anomalie: sleep interrompu prématurément (45/60 s)
```

### **Indicateurs de performance**
- Durée moyenne des cycles de sleep
- Taux de réveils prématurés
- Fréquence des erreurs de reconnexion WiFi
- Efficacité énergétique globale

## Tests recommandés

### 1. **Test de détection d'activité**
- Activer différents actionneurs
- Vérifier que le sleep est différé
- Tester les délais d'activité

### 2. **Test du sleep adaptatif**
- Simuler des erreurs
- Tester en période nocturne
- Vérifier les ajustements de délai

### 3. **Test de validation**
- Modifier les niveaux d'eau
- Changer la température
- Vérifier que le sleep est annulé

### 4. **Test de récupération**
- Simuler des échecs de réveil
- Tester la reconnexion WiFi
- Vérifier la détection d'anomalies

## Conclusion

Ces améliorations transforment le système de light sleep d'un simple timer en un système intelligent qui s'adapte aux conditions réelles du système. L'optimisation énergétique est significative tout en maintenant une fiabilité élevée et une facilité de maintenance. 

# Optimisation du Light-Sleep

Ce document complète les optimisations existantes par un affichage clair des causes de mise en veille et évite les superpositions sur l'OLED.

## Nouveautés OLED

### Écran de raison du light-sleep

Une nouvelle API dédiée réserve l'écran et affiche une cause explicite :

```cpp
// cause: texte court (ex: "Cause: inactivite", "Cause: maree montante", "Cause: decompte termine")
// detailLine1/detailLine2: deux lignes optionnelles (ex: "Inactivite depuis 300s", "Maree diff:0")
// lockMs: durée de verrouillage pour empêcher les superpositions
// mailBlink: true si un email est en cours d'envoi (affiche l'enveloppe clignotante)
_disp.showSleepReason(cause, detailLine1, detailLine2, 2500, mailBlink);
```

Caractéristiques :
- Efface l'écran et verrouille pendant `lockMs`.
- Affiche un en‑tête « Light-sleep », la cause et 2 détails.
- Force le dessin de la barre de statut (RSSI, marée, enveloppe si `mailBlink`).

### Éviter les superpositions

- Pendant un verrou (`lockScreen`), les vues de fond (`showMain`, `showVariables`, `showServerVars`, `drawStatus`) ne dessinent pas.
- La seule exception contrôlée est la barre de statut forçable via `drawStatusEx(..., force=true)` utilisée par `showSleepReason`.

## Intégration dans l'automatisme

- Les causes couvertes :
  - Inactivité prolongée
  - Marée montante
  - Fin de décompte (nourrissage, remplissage)
- L'icône enveloppe clignote si un email est en cours (`armMailBlink()`).

## Recommandations

- Toujours préférer `showSleepReason` pour les écrans de veille plutôt que des `showDiagnostic` multiples.
- Ajuster `lockMs` selon la criticité (2–3 s recommandé).
- Réinitialiser les caches (`resetStatusCache`, `resetMainCache`) au besoin après un écran dédié. 
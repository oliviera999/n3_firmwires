# Guide de Dépannage OLED - FFP3CS4

## Problème : OLED absente sur ESP32-WROOM

### Diagnostic

L'OLED n'est pas détectée car :
1. **L'ESP32-WROOM n'a pas d'OLED connectée** (c'est normal)
2. **Les pins I2C ne sont pas configurés correctement**
3. **L'OLED est défectueuse ou mal connectée**

### Solutions

#### 1. Utiliser l'environnement ESP32-WROOM

```bash
# Lister les environnements disponibles
pio run --list-environments

# Compiler et flasher pour WROOM (exemple prod)
pio run -e wroom-prod -t upload
```

#### 2. Vérifier les pins I2C

**ESP32-WROOM (BOARD_WROOM) :**
- SDA: GPIO 21
- SCL: GPIO 22
- Adresse: 0x3C

**ESP32-S3 (BOARD_S3) :**
- SDA: GPIO 8
- SCL: GPIO 9
- Adresse: 0x3C

#### 3. Connexion physique de l'OLED

Si vous voulez connecter une OLED à l'ESP32-WROOM :

```
ESP32-WROOM    OLED SSD1306
VCC (3.3V)  -> VCC
GND         -> GND
GPIO 21     -> SDA
GPIO 22     -> SCL
```

#### 4. Détection automatique

Le code détecte automatiquement si l'OLED est présente :

```
[OLED] Non détectée sur I2C (SDA:21, SCL:22, ADDR:0x3C) - Erreur: 1
```

**Codes d'erreur I2C :**
- 0: Succès (OLED détectée)
- 1: Buffer trop petit
- 2: NACK sur transmission d'adresse
- 3: NACK sur transmission de données
- 4: Autre erreur
- 5: Timeout

#### 5. Test de l'I2C

Pour tester les connexions I2C :

```cpp
// Dans le code Arduino
Wire.begin(21, 22);  // SDA, SCL
Wire.beginTransmission(0x3C);
byte error = Wire.endTransmission();
Serial.printf("Erreur I2C: %d\n", error);
```

### Configuration actuelle

Le projet utilise maintenant :
- **Environnements WROOM** : `wroom-prod`, `wroom-test`, `wroom-dev`
- **Pins I2C (WROOM)** : GPIO 21 (SDA), GPIO 22 (SCL)
- **Détection automatique** : L'OLED est détectée au démarrage
- **Mode dégradé** : Le système fonctionne sans OLED

### Commandes utiles

```bash
# Moniteur série
pio device monitor

# Nettoyer le projet
pio run -t clean
```

### Logs attendus

**Avec OLED connectée :**
```
[OLED] Initialisée avec succès sur I2C (SDA:21, SCL:22, ADDR:0x3C)
```

**Sans OLED :**
```
[OLED] Non détectée sur I2C (SDA:21, SCL:22, ADDR:0x3C) - Erreur: 2
```

---

## Problème : Superposition de texte / écran figé

### Symptômes
- Texte de diagnostic/OTA qui mord sur l'écran principal
- Écran qui reste bloqué sur un splash ou un diagnostic

### Causes probables
- Rendus de fond actifs pendant un écran réservé (diagnostic, OTA, raison de sleep)
- Absence de flush utile ou absence de verrouillage

### Correctifs et vérifications
- Utiliser les écrans dédiés avec verrouillage :
  - `showSleepReason(cause, l1, l2, 2500, mailBlink)`
  - `showOtaProgressEx(...)`
- S'assurer que `isLocked()` est respecté par les vues de fond (c'est le cas par défaut).
- En cas de besoin d'overlay statutaire pendant un verrou, utiliser `drawStatusEx(..., /*force=*/true)` au lieu de `drawStatus()`.
- Après un écran réservé, réinitialiser les caches si l'on observe des résidus :
  - `resetStatusCache()` et `resetMainCache()`

### Bonnes pratiques
- Grouper avec `beginUpdate()` / `endUpdate()` pour éviter les flushs multiples.
- Ne pas appeler `showMain/showVariables` pendant un écran verrouillé.
- Laisser expirer automatiquement le verrou (il s'auto-réinitialise). 
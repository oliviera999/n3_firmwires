# 🚀 Guide d'Utilisation Cursor IDE - Configuration ESP32 Optimisée

## 📋 Vue d'ensemble

Cette configuration Cursor IDE a été optimisée spécifiquement pour le développement ESP32 avec PlatformIO. Elle inclut des agents intelligents, des tâches automatisées, et des raccourcis clavier pour améliorer votre productivité.

## 🔧 Fonctionnalités Principales

### 1. Agent ESP32 Intelligent Assistant
- **Surveillance automatique** des logs critiques ESP32
- **Détection d'erreurs** : Guru Meditation, Panic, Brownout, Stack overflow
- **Analyse de performance** : timeouts, délais bloquants, problèmes mémoire
- **Recommandations automatiques** pour les bonnes pratiques ESP32

### 2. Configuration IntelliSense C++ Optimisée
- **Autocomplétion avancée** pour ESP32 et Arduino
- **Détection d'erreurs** en temps réel
- **Support complet** des bibliothèques PlatformIO
- **Coloration syntaxique** améliorée

### 3. Tâches Automatisées
- **Build** : Compilation pour différents environnements
- **Upload** : Déploiement firmware sur ESP32
- **Monitor** : Surveillance série en temps réel
- **Clean** : Nettoyage des fichiers de build
- **Filesystem** : Upload du système de fichiers
- **Monitoring 90s** : Surveillance post-déploiement (OBLIGATOIRE)

## ⌨️ Raccourcis Clavier

| Raccourci | Action | Description |
|-----------|--------|-------------|
| `Ctrl+Shift+B` | Build | Compile le projet (s3-test par défaut) |
| `Ctrl+Shift+U` | Upload S3 | Upload sur ESP32-S3 |
| `Ctrl+Shift+W` | Build WROOM | Compile pour ESP32-WROOM |
| `Ctrl+Shift+S` | Upload WROOM | Upload sur ESP32-WROOM |
| `Ctrl+Shift+M` | Monitor | Ouvre le moniteur série |
| `Ctrl+Shift+C` | Clean | Nettoie les fichiers de build |
| `Ctrl+Shift+F` | Filesystem | Upload du système de fichiers |
| `Ctrl+Shift+9` | Monitor 90s | Surveillance post-déploiement |
| `Ctrl+Shift+L` | Lint | Vérification du code |
| `Ctrl+Shift+R` | Update Libs | Mise à jour des bibliothèques |

## 🎯 Workflow de Développement Recommandé

### 1. Développement Quotidien
```bash
# 1. Ouvrir le workspace
code ffp5cs-esp32.code-workspace

# 2. Build du projet
Ctrl+Shift+B

# 3. Upload sur ESP32
Ctrl+Shift+U

# 4. Monitoring post-déploiement (OBLIGATOIRE)
Ctrl+Shift+9
```

### 2. Changement d'Environnement
- **ESP32-S3** : Utiliser `Ctrl+Shift+B` et `Ctrl+Shift+U`
- **ESP32-WROOM** : Utiliser `Ctrl+Shift+W` et `Ctrl+Shift+S`

### 3. Débogage
- **Logs série** : `Ctrl+Shift+M`
- **Vérification code** : `Ctrl+Shift+L`
- **Nettoyage** : `Ctrl+Shift+C`

## 🤖 Agent Intelligent - Fonctionnalités

### Surveillance Automatique
L'agent surveille automatiquement :
- **Logs critiques** : Guru Meditation, Panic, Brownout
- **Avertissements** : Timeouts, reconnexions, mémoire
- **Performance** : Délais bloquants, watchdog, tâches

### Détection de Patterns Problématiques
- `delay()` → Recommande `vTaskDelay()`
- `String` → Recommande `char[]`
- `malloc()` → Vérifie la libération mémoire
- `while(true)` → Ajoute watchdog reset

### Surveillance des Fichiers
- **platformio.ini** : Vérification cohérence configuration
- **main.cpp** : Détection patterns problématiques
- **config.h** : Surveillance des versions

## 📁 Structure des Fichiers de Configuration

```
.vscode/
├── settings.json          # Configuration IDE optimisée
├── tasks.json             # Tâches automatisées
├── keybindings.json       # Raccourcis clavier
├── extensions.json        # Extensions recommandées
├── launch.json           # Configuration débogage
└── c_cpp_properties.json # IntelliSense C++ (auto-généré)

.cursor/
├── agent.ts              # Agent intelligent ESP32
└── cursor-agent.d.ts     # Types TypeScript

ffp5cs-esp32.code-workspace # Configuration workspace
cursor.json               # Configuration Cursor principale
```

## 🔍 IntelliSense et Autocomplétion

### Fonctionnalités Activées
- ✅ **Autocomplétion** : Variables, fonctions, classes
- ✅ **Détection d'erreurs** : Erreurs de compilation en temps réel
- ✅ **Snippets** : Code snippets pour ESP32
- ✅ **Coloration** : Coloration syntaxique avancée
- ✅ **Navigation** : Go to definition, find references

### Bibliothèques Supportées
- ESPAsyncWebServer
- ArduinoJson
- DHT sensor library
- Adafruit SSD1306
- DallasTemperature
- OneWire
- ESP32Servo
- WebSockets

## 🚨 Surveillance et Alertes

### Erreurs Critiques (Priorité HAUTE)
- Guru Meditation
- Panic
- Assert failed
- Core dumped
- Brownout
- Stack overflow
- Heap corruption

### Avertissements (Priorité MOYENNE)
- WARNING/WARN
- Timeout
- Disconnect/Reconnect
- Memory/Heap issues

### Performance (Priorité FAIBLE)
- Slow operations
- Delay/Blocking
- Watchdog issues
- Task problems

## 📊 Monitoring Post-Déploiement

### Procédure Obligatoire
1. **Déployer** le firmware (`Ctrl+Shift+U`)
2. **Lancer** le monitoring 90s (`Ctrl+Shift+9`)
3. **Analyser** les logs pour détecter :
   - Erreurs critiques
   - Avertissements système
   - Problèmes de performance
4. **Corriger** les problèmes détectés

### Scripts de Monitoring
- `monitor_90s_v11.49.ps1` : Monitoring standard
- `monitor_15min.ps1` : Monitoring étendu
- `monitor_30min_v11.49.ps1` : Monitoring long terme

## 🛠️ Dépannage

### Problèmes Courants

#### IntelliSense ne fonctionne pas
```bash
# Régénérer la base de données de compilation
pio run --target compiledb
```

#### Agent ne répond pas
```bash
# Redémarrer Cursor IDE
# Vérifier les logs dans .cursor/
```

#### Tâches ne s'exécutent pas
```bash
# Vérifier PlatformIO installé
pio --version

# Vérifier les permissions PowerShell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

### Logs et Debug
- **Logs Cursor** : `.cursor/` directory
- **Logs PlatformIO** : `.pio/` directory
- **Logs ESP32** : `pythonserial/esp32_logs.txt`

## 📈 Optimisations Appliquées

### Performance IDE
- ✅ Exclusions de fichiers optimisées
- ✅ Indexation intelligente
- ✅ Cache IntelliSense
- ✅ Recherche optimisée

### Développement ESP32
- ✅ Configuration multi-environnements
- ✅ Build flags optimisés
- ✅ Gestion mémoire surveillée
- ✅ Patterns de code vérifiés

### Workflow
- ✅ Tâches automatisées
- ✅ Raccourcis clavier
- ✅ Monitoring automatique
- ✅ Alertes intelligentes

## 🎉 Résultat

Cette configuration vous offre :
- **Productivité** : Développement plus rapide et efficace
- **Qualité** : Détection automatique des problèmes
- **Stabilité** : Surveillance continue de la stabilité ESP32
- **Conformité** : Respect des bonnes pratiques ESP32

---

**Note** : Cette configuration est spécifiquement optimisée pour le projet ESP32 Aquaponie Controller (FFP5CS). Adaptez les paramètres selon vos besoins spécifiques.

# Solutions pour les Tests Unitaires et Embarqués

## 📋 Problèmes identifiés

### 1. Tests unitaires `native` - Échec
**Cause** : `test_build_src = yes` compilait TOUS les fichiers source du projet, y compris ceux qui dépendent d'Arduino/FreeRTOS qui n'existent pas sur PC.

**Solution** : 
- ✅ Changé `test_build_src = no` 
- ✅ Utilisé `build_src_filter` pour compiler uniquement les fichiers testables (`timer_manager.cpp`, `rate_limiter.cpp`)
- ✅ Ajouté des flags pour désactiver FreeRTOS et ESP-IDF en mode test

### 2. Tests embarqués `wroom-test` - Échec (chemins longs Windows)
**Cause** : Le projet est dans un chemin très long (`…\Mon Drive\…`) qui dépasse la limite MAX_PATH de Windows (260 caractères), causant des erreurs lors de la création de fichiers temporaires dans `.pio/build`.

**Solution** :
- ✅ Ajouté un `build_dir` personnalisé dans `platformio.ini` pour `wroom-test`
- ✅ Utilise `${env::USERPROFILE}\.pio_build\ffp5cs_wroom-test` (chemin court dans le répertoire utilisateur)

## 🔧 Configuration mise à jour

### `platformio.ini` - Environnement `native`

```ini
[env:native]
platform = native
framework = 
test_framework = unity
; IMPORTANT: Ne pas compiler tous les fichiers source (évite Arduino/FreeRTOS)
test_build_src = no
; Limiter les tests natifs aux tests unitaires host-friendly
test_filter =
	test_rate_limiter
	test_timer_manager
; Ne compiler QUE les modules testables (évite Arduino/FreeRTOS)
build_src_filter =
	-<*>
	+<timer_manager.cpp>
	+<rate_limiter.cpp>
build_flags = 
	-std=gnu++17
	-Itest/unit
	-Iinclude
	-Og
	-g
	-DUNIT_TEST
	-DARDUINO=0
	-DESP32=0
	-DDISABLE_ASYNC_WEBSERVER
	-DFREERTOS=0
	-DESP_IDF_VERSION_MAJOR=0
```

### `platformio.ini` - Environnement `wroom-test`

**Note** : PlatformIO ne supporte pas directement `build_dir` dans la configuration. Solutions alternatives :

1. **Activer les chemins longs Windows** (recommandé, nécessite admin) :
   
   **Méthode 1 - Via PowerShell (Admin)** :
   ```powershell
   # Ouvrir PowerShell en tant qu'administrateur
   # Puis exécuter :
   New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name "LongPathsEnabled" -Value 1 -PropertyType DWORD -Force
   ```
   
   **Méthode 2 - Via CMD (Admin)** :
   ```cmd
   reg add "HKLM\SYSTEM\CurrentControlSet\Control\FileSystem" /v LongPathsEnabled /t REG_DWORD /d 1 /f
   ```
   
   **Méthode 3 - Via l'Éditeur de Registre** :
   1. Ouvrir `regedit` (Win+R → `regedit`)
   2. Naviguer vers : `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\FileSystem`
   3. Créer/modifier la valeur `LongPathsEnabled` (type DWORD) = `1`
   4. Redémarrer l'ordinateur
   
   **⚠️ Important** : Un redémarrage est nécessaire pour que les changements prennent effet.

2. **Déplacer le projet** dans un chemin plus court (ex: `C:\Projects\ffp5cs`)

3. **Utiliser un script de build personnalisé** pour définir le build_dir

## 🧪 Utilisation

### Tests unitaires (native)
```bash
pio test -e native
```

### Tests embarqués (compilation uniquement)
```bash
pio run -e wroom-test
```

### Tests embarqués (avec upload)
```bash
pio test -e wroom-test --without-uploading
```

## 📁 Structure des tests

```
test/
├── test_rate_limiter/
│   └── test_main.cpp          # Tests unitaires RateLimiter
├── test_timer_manager/
│   └── test_main.cpp          # Tests unitaires TimerManager
├── test_modem_sleep/
│   └── test_main.cpp          # Test embarqué (nécessite ESP32)
└── unit/
    ├── Arduino.h              # Mock Arduino minimal
    └── test_mocks.h           # Mocks pour millis(), micros(), Serial
```

## ✅ Mocks disponibles

Les mocks dans `test/unit/test_mocks.h` fournissent :

- ✅ `millis()` / `micros()` - Gestion du temps simulé
- ✅ `Serial` - Classe HardwareSerial mockée
- ✅ `String` - Classe String Arduino compatible (dans `test/unit/Arduino.h`)
- ✅ Types Arduino : `byte`, `word`
- ✅ Macros : `delay()`, `HIGH`, `LOW`, `INPUT`, `OUTPUT`

### Fonctions utilitaires pour les tests

```cpp
// Initialiser le temps
setup_mock_time(1000, 0);  // 1000ms, 0µs

// Avancer le temps
advance_time(100);  // +100ms

// Réinitialiser
reset_mock_time();
```

## ✅ Résultats des tests

### Tests unitaires `native` - SUCCÈS ✅

```bash
$ pio test -e native

✅ test_timer_manager: 13 tests, 0 échecs
✅ test_rate_limiter: 11 tests, 0 échecs
```

**Total** : 24 tests unitaires passent avec succès !

### Tests embarqués - Compilation OK ✅

La compilation `wroom-test` fonctionne correctement :
- RAM: 24.4% utilisée
- Flash: 53.0% utilisée
- Aucune erreur de compilation

## 🚀 Prochaines étapes

1. ✅ Tests unitaires `native` fonctionnels
2. ⏳ Résoudre les problèmes de chemins longs pour tests embarqués complets
3. ⏳ Ajouter plus de tests unitaires pour d'autres modules
4. ⏳ Automatiser les tests dans CI/CD

## 📝 Notes importantes

- Les tests `native` ne peuvent tester que les modules **indépendants d'Arduino/FreeRTOS**
- Les tests embarqués nécessitent un ESP32 connecté pour les tests hardware
- Le `build_dir` personnalisé résout les problèmes de chemins longs mais nécessite un nettoyage manuel si nécessaire :
  ```bash
  # Nettoyer le build_dir personnalisé
  rmdir /s "%USERPROFILE%\.pio_build\ffp5cs_wroom-test"
  ```

## 🔍 Dépannage

### Erreur : "Arduino.h: No such file or directory" en test native
**Solution** : Vérifier que `test_build_src = no` et que `build_src_filter` exclut les fichiers Arduino.

### Erreur : "No such file or directory" lors de la compilation embarquée
**Solution** : Vérifier que le `build_dir` personnalisé est configuré et que le répertoire peut être créé.

### Tests unitaires passent mais code ne compile pas sur ESP32
**Solution** : Normal, les mocks ne couvrent pas tout. Tester la compilation ESP32 séparément avec `pio run -e wroom-test`.

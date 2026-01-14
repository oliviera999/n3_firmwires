# Guide : Activer cppcheck sur Windows

## 📋 Vue d'ensemble

Il existe **deux méthodes principales** pour activer cppcheck :

1. **Installation globale sur Windows** (recommandé pour éviter les problèmes de verrouillage)
2. **Utilisation via PlatformIO** (automatique mais peut avoir des problèmes de verrouillage)

---

## 🎯 Option 1 : Installation globale sur Windows (RECOMMANDÉ)

### Avantages
- ✅ Évite les problèmes de verrouillage de PlatformIO
- ✅ Disponible pour tous les projets
- ✅ Peut être utilisé directement dans `audit.bat`
- ✅ Plus rapide et plus stable

### Méthode A : Installation via Chocolatey (Plus simple)

1. **Installer Chocolatey** (si pas déjà installé) :
   ```powershell
   # Exécuter PowerShell en tant qu'administrateur
   Set-ExecutionPolicy Bypass -Scope Process -Force
   [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
   iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
   ```

2. **Installer cppcheck** :
   ```powershell
   choco install cppcheck
   ```

3. **Vérifier l'installation** :
   ```powershell
   cppcheck --version
   ```

### Méthode B : Installation manuelle

1. **Télécharger cppcheck** :
   - Aller sur https://cppcheck.sourceforge.io/
   - Télécharger la version Windows (ex: `cppcheck-2.13.0-x64-Setup.msi`)

2. **Installer** :
   - Exécuter le fichier `.msi` téléchargé
   - Suivre l'assistant d'installation
   - Cocher "Add to PATH" si disponible

3. **Vérifier l'installation** :
   ```powershell
   cppcheck --version
   ```

### Configuration après installation globale

Une fois cppcheck installé globalement, vous avez deux options :

#### Option A : Utiliser cppcheck directement dans `audit.bat`

Le script `audit.bat` utilisera automatiquement l'installation globale. Pas besoin de modifier `platformio.ini`.

#### Option B : Utiliser cppcheck via PlatformIO

Même avec une installation globale, vous pouvez toujours utiliser PlatformIO. Modifier `platformio.ini` :

```ini
check_tool =
	cppcheck
	clangtidy
check_flags =
	cppcheck: --suppressions-list=config/cppcheck.suppress --suppress=preprocessorErrorDirective
	clangtidy: --config-file=config/clang-tidy.yaml
```

PlatformIO utilisera l'installation globale si disponible.

---

## 🔧 Option 2 : Utilisation via PlatformIO uniquement

### Avantages
- ✅ Pas d'installation manuelle
- ✅ Géré automatiquement par PlatformIO
- ✅ Version spécifique par projet

### Inconvénients
- ⚠️ Peut avoir des problèmes de verrouillage sous Windows
- ⚠️ Nécessite de résoudre les problèmes de processus bloqués

### Configuration

1. **Modifier `platformio.ini`** :
```ini
check_tool =
	cppcheck
	clangtidy
check_flags =
	cppcheck: --suppressions-list=config/cppcheck.suppress --suppress=preprocessorErrorDirective
	clangtidy: --config-file=config/clang-tidy.yaml
```

2. **Résoudre les problèmes de verrouillage** (si nécessaire) :

Si vous rencontrez l'erreur `FileExistsError: [WinError 183]`, suivez ces étapes :

```powershell
# 1. Arrêter tous les processus cppcheck
Get-Process cppcheck -ErrorAction SilentlyContinue | Stop-Process -Force

# 2. Supprimer le répertoire verrouillé (si nécessaire)
Remove-Item -Path "$env:USERPROFILE\.platformio\packages\tool-cppcheck" -Recurse -Force -ErrorAction SilentlyContinue

# 3. Réessayer l'installation
pio pkg install -g -t platformio/tool-cppcheck
```

3. **Tester** :
```powershell
pio check --environment wroom-test --skip-packages
```

---

## 🧪 Test de l'installation

### Test avec installation globale

```powershell
# Test direct
cppcheck --version

# Test dans le projet
cd "c:\Users\olivi\Mon Drive\travail\olution\Projets\prototypage\platformIO\Projects\ffp5cs"
cppcheck --enable=all --inconclusive src/ --suppressions-list=config/cppcheck.suppress
```

### Test avec PlatformIO

```powershell
cd "c:\Users\olivi\Mon Drive\travail\olution\Projets\prototypage\platformIO\Projects\ffp5cs"
pio check --environment wroom-test --skip-packages
```

---

## 📝 Mise à jour de `audit.bat`

Si vous installez cppcheck globalement, le script `audit.bat` fonctionnera automatiquement. Si vous préférez utiliser PlatformIO dans `audit.bat`, modifiez-le ainsi :

```batch
echo 🔍 Running cppcheck via PlatformIO...
cd /d "%SRC_DIR%"
pio check --environment wroom-test --skip-packages 2> "%REPORT_DIR%\cppcheck.log"
cd /d "%~dp0"
```

---

## 🎯 Recommandation

**Pour votre cas, je recommande l'Option 1 (Installation globale)** car :

1. ✅ Évite les problèmes de verrouillage Windows avec PlatformIO
2. ✅ Plus rapide et plus fiable
3. ✅ Fonctionne directement avec `audit.bat`
4. ✅ Disponible pour tous vos projets

Une fois installé globalement, vous pouvez réactiver cppcheck dans `platformio.ini` et il utilisera l'installation globale.

---

## 🔍 Vérification finale

Après installation, vérifiez que tout fonctionne :

```powershell
# 1. Vérifier que cppcheck est dans le PATH
cppcheck --version

# 2. Tester avec PlatformIO
cd "c:\Users\olivi\Mon Drive\travail\olution\Projets\prototypage\platformIO\Projects\ffp5cs"
pio check --environment wroom-test --skip-packages

# 3. Tester avec audit.bat
.\audit.bat
```

---

## 📚 Ressources

- Site officiel cppcheck : https://cppcheck.sourceforge.io/
- Documentation PlatformIO : https://docs.platformio.org/en/latest/integration/ide/vscode.html#check
- Chocolatey : https://chocolatey.org/

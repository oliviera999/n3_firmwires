# Activer les Chemins Longs Windows

## 🎯 Pourquoi activer les chemins longs ?

Votre projet est situé dans un chemin très long :
```
C:\Users\olivi\Mon Drive\travail\olution\Projets\prototypage\platformIO\Projects\ffp5cs
```

Windows limite par défaut les chemins à **260 caractères** (MAX_PATH). Cette limitation peut causer des erreurs lors de la compilation PlatformIO avec des chemins de build très longs.

## ✅ Solution : Activer les chemins longs Windows

### Méthode 1 : Script PowerShell (Recommandé) ⭐

1. **Ouvrir PowerShell en tant qu'administrateur** :
   - Clic droit sur le menu Démarrer → "Windows PowerShell (Admin)" ou "Terminal (Admin)"
   - Ou : `Win + X` → Sélectionner "Terminal (Admin)" ou "PowerShell (Admin)"

2. **Exécuter le script** :
   ```powershell
   cd "C:\Users\olivi\Mon Drive\travail\olution\Projets\prototypage\platformIO\Projects\ffp5cs"
   .\scripts\enable_long_paths.ps1
   ```

3. **Redémarrer l'ordinateur** (obligatoire pour que les changements prennent effet)

### Méthode 2 : Commande PowerShell directe

1. **Ouvrir PowerShell en tant qu'administrateur**

2. **Exécuter la commande** :
   ```powershell
   New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name "LongPathsEnabled" -Value 1 -PropertyType DWORD -Force
   ```

3. **Redémarrer l'ordinateur**

### Méthode 3 : Commande CMD

1. **Ouvrir CMD en tant qu'administrateur** :
   - `Win + X` → "Invite de commandes (Admin)" ou "Terminal (Admin)"

2. **Exécuter la commande** :
   ```cmd
   reg add "HKLM\SYSTEM\CurrentControlSet\Control\FileSystem" /v LongPathsEnabled /t REG_DWORD /d 1 /f
   ```

3. **Redémarrer l'ordinateur**

### Méthode 4 : Éditeur de Registre (GUI)

1. **Ouvrir l'Éditeur de Registre** :
   - `Win + R` → Taper `regedit` → Entrée
   - Confirmer l'UAC si demandé

2. **Naviguer vers** :
   ```
   HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\FileSystem
   ```

3. **Créer/modifier la valeur** :
   - Clic droit dans le panneau de droite → "Nouveau" → "Valeur DWORD (32 bits)"
   - Nom : `LongPathsEnabled`
   - Valeur : `1`
   - Double-clic pour modifier si elle existe déjà

4. **Redémarrer l'ordinateur**

## ✅ Vérifier l'activation

Après redémarrage, vérifier que c'est bien activé :

**PowerShell** :
```powershell
Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name "LongPathsEnabled"
```

**CMD** :
```cmd
reg query "HKLM\SYSTEM\CurrentControlSet\Control\FileSystem" /v LongPathsEnabled
```

La valeur doit être `1` (ou `0x1`).

## ⚠️ Notes importantes

1. **Redémarrage obligatoire** : Les changements ne prennent effet qu'après redémarrage
2. **Privilèges administrateur requis** : Toutes les méthodes nécessitent des droits admin
3. **Compatibilité** : Windows 10 version 1607+ et Windows 11
4. **Applications** : Certaines applications doivent être "long path aware" pour bénéficier de cette fonctionnalité

## 🔍 Après activation

Une fois activé et après redémarrage, vous pourrez :
- ✅ Compiler sans erreurs de chemins longs
- ✅ Utiliser `pio test -e wroom-test` sans problèmes
- ✅ Créer des fichiers dans des chemins très longs

## 🆘 Dépannage

### Erreur "Accès refusé"
→ Exécuter en tant qu'administrateur

### Erreur "La clé n'existe pas"
→ Normal, elle sera créée automatiquement

### Les chemins longs ne fonctionnent toujours pas après redémarrage
→ Vérifier que la valeur est bien `1` dans le registre
→ Certaines applications (comme Windows Explorer) peuvent encore avoir des limitations

## 📚 Références

- [Documentation Microsoft - Long Paths](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)
- [Script d'activation](scripts/enable_long_paths.ps1)

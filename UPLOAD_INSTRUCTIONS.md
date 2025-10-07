# 📤 Instructions d'Upload sur l'ESP32

## ✅ Migration Terminée

Tous les fichiers sont prêts dans le dossier `data/` !

---

## 📦 Fichiers à Uploader

### ✅ Fichiers Essentiels (OBLIGATOIRES)

```
data/
├── index.html                    ← Page d'accueil (NOUVEAU)
├── manifest.json
├── sw.js
│
├── pages/                        ← 4 nouvelles pages
│   ├── dashboard.html
│   ├── controles.html
│   ├── reglages.html
│   └── wifi.html
│
├── shared/                       ← Ressources partagées (NOUVEAU)
│   ├── common.css
│   ├── common.js
│   └── websocket.js
│
└── assets/
    ├── css/
    │   └── uplot.min.css
    └── js/
        └── uplot.iife.min.js
```

### 📝 Fichiers Optionnels (ne pas uploader)

- `index_old_monolithic.html` → Sauvegarde locale
- `index_old.html` → Ancien fichier (peut être supprimé)
- `index_temp.html` → Fichier temporaire (peut être supprimé)
- `README_STRUCTURE.md` → Documentation locale
- `desktop.ini` → Fichiers système Windows (ignorés)

---

## 🚀 Méthode 1 : PlatformIO (Recommandé)

### Option A : Upload Complet du Filesystem

```bash
# Dans le terminal PlatformIO
pio run -t uploadfs
```

Cette commande uploade automatiquement tout le contenu du dossier `data/` sur l'ESP32.

### Option B : Upload + Build + Upload Firmware

```bash
# Upload filesystem + firmware en une commande
pio run -t uploadfs && pio run -t upload
```

---

## 🌐 Méthode 2 : Interface Web OTA

Si votre ESP32 est déjà en ligne :

### 1. Accédez à l'interface OTA
```
http://esp32.local/update
ou
http://192.168.x.x/update
```

### 2. Uploadez les fichiers UN PAR UN

⚠️ **Important** : L'interface OTA nécessite d'uploader chaque fichier individuellement.

**Ordre recommandé :**

1. **D'abord les ressources partagées** (pour éviter les erreurs 404)
   - `shared/common.css`
   - `shared/common.js`
   - `shared/websocket.js`

2. **Ensuite les pages**
   - `pages/dashboard.html`
   - `pages/controles.html`
   - `pages/reglages.html`
   - `pages/wifi.html`

3. **Enfin la page d'accueil**
   - `index.html` (remplacera l'ancien)

4. **Assets (si nécessaire)**
   - `assets/css/uplot.min.css`
   - `assets/js/uplot.iife.min.js`

---

## 🧪 Test Local (Avant Upload)

Vous pouvez tester la nouvelle structure localement :

### Option 1 : Ouvrir directement dans le navigateur

```bash
# Ouvrez dans votre navigateur
data/index.html
```

⚠️ **Limitation** : Le WebSocket ne fonctionnera pas localement (normal), mais vous pouvez vérifier :
- La navigation entre les pages
- Le design et les styles
- La structure HTML

### Option 2 : Serveur local Python

```bash
# Dans le dossier data/
cd data
python -m http.server 8000
```

Puis ouvrez `http://localhost:8000` dans votre navigateur.

---

## ✅ Vérification Après Upload

### 1. Test de Base

1. Accédez à `http://esp32.local/`
2. Vous devriez voir la **nouvelle page d'accueil** avec :
   - Le logo 🐠 et le titre "FFP3 Dashboard"
   - Des boutons "Voir les Mesures", "Contrôles Manuels", etc.
   - Un aperçu rapide des températures
   - Des actions rapides

### 2. Test de Navigation

Cliquez sur chaque lien et vérifiez :
- ✅ `📈 Mesures` → Affiche les graphiques temps réel
- ✅ `🎛️ Contrôles` → Affiche les boutons de contrôle
- ✅ `📋 Réglages` → Affiche le formulaire de configuration
- ✅ `📶 WiFi` → Affiche la gestion WiFi

### 3. Test des Fonctionnalités

- ✅ Les données s'affichent (températures, niveaux)
- ✅ Les graphiques se dessinent
- ✅ Les boutons répondent aux clics
- ✅ Le WebSocket se connecte (statut "En ligne")

### 4. Vérification Console

Ouvrez la console JavaScript (F12) et vérifiez qu'il n'y a pas d'erreurs :
- ❌ Pas d'erreur 404 (fichiers non trouvés)
- ❌ Pas d'erreur JavaScript
- ✅ Message "[INIT] Dashboard consolidé initialisé"

---

## 🐛 Résolution de Problèmes

### ❌ Erreur 404 sur les fichiers CSS/JS

**Symptôme** : Les styles ne s'appliquent pas, page blanche

**Solution** :
1. Vérifiez que `shared/common.css` est bien uploadé
2. Vérifiez les chemins dans le code source (F12 > Réseau)
3. Réuploadez le fichier manquant

### ❌ JavaScript ne fonctionne pas

**Symptôme** : Les boutons ne font rien, pas de données

**Solution** :
1. Ouvrez la console (F12)
2. Vérifiez les erreurs JavaScript
3. Assurez-vous que `shared/common.js` et `shared/websocket.js` sont uploadés
4. Videz le cache navigateur (Ctrl+Shift+R)

### ❌ WebSocket ne se connecte pas

**Symptôme** : Statut reste "Hors ligne"

**Solution** :
1. Vérifiez que `shared/websocket.js` est bien uploadé
2. Vérifiez la console pour les erreurs de connexion
3. Testez le port 81 : `ws://esp32.local:81/ws`
4. Redémarrez l'ESP32

### ❌ Page blanche

**Symptôme** : Rien ne s'affiche

**Solution** :
1. Vérifiez que `index.html` est bien uploadé
2. Ouvrez la console (F12) pour voir les erreurs
3. Vérifiez que tous les fichiers `shared/` sont présents
4. Réuploadez tout le filesystem

---

## 🔄 Retour en Arrière (si nécessaire)

Si vous voulez revenir à l'ancien fichier monolithique :

### Via PlatformIO

```bash
# 1. Restaurer l'ancien fichier
cd data
Copy-Item index_old_monolithic.html index.html -Force

# 2. Re-uploader
pio run -t uploadfs
```

### Via Interface Web

1. Accédez à `/update`
2. Uploadez `index_old_monolithic.html` comme `index.html`

---

## 📊 Espace Filesystem

Vérifiez l'espace disponible sur votre ESP32 :

```bash
# Voir la taille du filesystem
pio run -t size
```

**Estimations** :
- Ancien système : ~150 Ko (1 fichier)
- Nouveau système : ~180 Ko (9 fichiers)
- **Différence** : +30 Ko (acceptable)

Si vous manquez d'espace :
1. Supprimez les anciens fichiers (`index_old.html`, `index_temp.html`)
2. Utilisez la compression gzip (activez dans le code serveur)
3. Minifiez les fichiers CSS/JS

---

## 🎯 Commande Rapide (Tout en Un)

```bash
# Upload filesystem complet sur l'ESP32
cd "C:\Users\olivi\Mon Drive\travail\##olution\##Projets\##prototypage\platformIO\Projects\ffp5cs"
pio run -t uploadfs
```

**Durée estimée** : 30-60 secondes selon la vitesse de connexion USB.

---

## 📝 Checklist Finale

Avant de considérer la migration terminée :

- [ ] Tous les fichiers uploadés sur l'ESP32
- [ ] Page d'accueil accessible sur `http://esp32.local/`
- [ ] Navigation entre les pages fonctionne
- [ ] Données temps réel s'affichent
- [ ] Graphiques fonctionnent
- [ ] Boutons de contrôle répondent
- [ ] Configuration WiFi accessible
- [ ] Pas d'erreurs dans la console (F12)
- [ ] WebSocket se connecte (statut "En ligne")

---

## ✨ Après la Migration

Une fois tout testé et validé, vous pouvez :

1. **Nettoyer** : Supprimer les fichiers temporaires
   ```bash
   cd data
   Remove-Item index_old.html, index_temp.html -Force
   ```

2. **Documenter** : Garder `README_STRUCTURE.md` pour référence

3. **Sauvegarder** : Faire un commit Git
   ```bash
   git add data/
   git commit -m "✨ Migration vers architecture modulaire"
   ```

4. **Profiter** : Votre dashboard est maintenant moderne et maintenable ! 🚀

---

## 🆘 Besoin d'Aide ?

Si vous rencontrez des problèmes :

1. **Console JavaScript** (F12) → Voir les erreurs détaillées
2. **Network Tab** (F12) → Vérifier les fichiers chargés (ou manquants)
3. **Serial Monitor** → Voir les logs de l'ESP32
4. **Documentation** → Consultez `README_STRUCTURE.md`

---

**Bonne migration ! 🎉**


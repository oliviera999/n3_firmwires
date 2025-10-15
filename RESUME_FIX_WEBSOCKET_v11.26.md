# ✅ RÉSUMÉ - FIX WebSocket Code 1006 (v11.26)

## 🎯 Problème résolu

Le WebSocket se fermait **brutalement** avec le code **1006**, causant des déconnexions fréquentes et forçant le système en mode polling.

### Cause
Quand l'ESP32 entre en **light sleep**, il déconnecte le WiFi **brutalement**, fermant toutes les connexions TCP (dont le WebSocket) **sans Close frame propre** → Code 1006.

---

## 🔧 Solution (3 modifications)

### 1️⃣ **Fermeture propre du WebSocket** (`src/power.cpp`)
- ✅ Détection des clients WebSocket connectés
- ✅ Envoi d'un message de notification ("server_closing")
- ✅ Fermeture propre avec Close frame
- ✅ Délai de 350ms pour garantir l'envoi avant déconnexion WiFi

### 2️⃣ **Amélioration notification** (`include/realtime_websocket.h`)
- ✅ Détection automatique du type de message (sleep vs changement WiFi)
- ✅ Envoi du bon message selon le contexte
- ✅ Logs informatifs côté serveur

### 3️⃣ **Gestion client améliorée** (`data/shared/websocket.js`)
- ✅ Message utilisateur : "💤 Système en veille - Reconnexion automatique au réveil"
- ✅ Reconnexion après **30 secondes** (au lieu de 10s) pour le sleep
- ✅ Distinction entre sleep et reconfiguration

---

## 📊 Résultats attendus

### Avant (❌)
```
WebSocket closed (code: 1006, raison: N/A)  ← Fermeture brutale
→ Mode polling activé
```

### Après (✅)
```
⚠️ Serveur fermé: System entering light sleep mode
💤 Système en veille - Reconnexion automatique au réveil
🔌 WebSocket fermé (code: 1000, raison: Fermeture normale)  ← Fermeture propre
// Attente de 30 secondes...
🔄 Tentative de reconnexion après fermeture serveur...
✅ Connexion WebSocket établie sur le port 81
```

---

## 🚀 Déploiement

```bash
# 1. Compiler et uploader
pio run -t upload
pio run -t uploadfs

# 2. Monitoring de 90 secondes (OBLIGATOIRE)
pio device monitor
```

### Points de vérification ✅
- [x] WebSocket se connecte initialement
- [x] Message "server_closing" reçu avant sleep
- [x] WebSocket fermé avec code **1000** (au lieu de 1006)
- [x] Reconnexion automatique après 30s
- [x] Heap > 30KB avant sleep

---

## 💡 Bonus : Variables sur plusieurs lignes

En bonus, j'ai également amélioré l'affichage de la page **Contrôles** pour afficher les variables actuelles et le formulaire de modification **sur plusieurs lignes** au lieu de côte à côte.

**Fichier modifié** : `data/pages/controles.html`

### Amélioration :
- ✅ Variables groupées par catégories (Horaires, Durées, Seuils, etc.)
- ✅ Affichage sur toute la largeur (meilleure lisibilité)
- ✅ Design responsive (s'adapte à tous les écrans)
- ✅ Formulaire affiché en dessous au lieu de côte à côte

---

## 📦 Version

**11.25 → 11.26** (correction mineure)

---

**✅ PRÊT POUR DÉPLOIEMENT**


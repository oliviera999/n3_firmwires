# 🔍 Vérification PISTE 1 - Logs de Diagnostic Ajoutés

**Date:** 2026-01-25  
**Objectif:** Vérifier si la configuration NVS désactive les flags réseau

---

## 📝 Modifications Apportées

### 1. Amélioration de `loadNetworkFlags()` (`src/config.cpp`)

**Ajouts:**
- Log détaillé indiquant si la valeur vient de NVS ou de la valeur par défaut
- Avertissement explicite si les flags sont désactivés
- Diagnostic PISTE 1 avec informations sur l'origine des valeurs

**Messages attendus au boot:**
```
[Config] 📥 Chargement flags réseau depuis NVS centralisé
[Config] ✅ Net flags chargés - send:ON recv:ON
[Config] 🔍 Diagnostic PISTE 1 - send: true (défaut: utilisé/NVS), recv: true (défaut: utilisé/NVS)
```

**Si désactivé:**
```
[Config] ⚠️ ATTENTION: Envoi distant DÉSACTIVÉ en NVS (net_send_en=false)
[Config] ⚠️ ATTENTION: Réception distante DÉSACTIVÉE en NVS (net_recv_en=false)
```

### 2. Log d'entrée dans `AutomatismSync::update()` (`src/automatism/automatism_sync.cpp`)

**Ajout:**
- Log au premier appel pour confirmer que la méthode est invoquée
- Affiche l'état WiFi et des flags réseau au premier appel

**Message attendu:**
```
[Sync] ⚠️ DEBUG PISTE 1: update() appelé pour la première fois
[Sync] DEBUG PISTE 1: WiFi=3, SendEnabled=1, RecvEnabled=1
```

**Interprétation:**
- Si ce message **n'apparaît pas** → `AutomatismSync::update()` n'est jamais appelé (PISTE 2)
- Si ce message **apparaît avec SendEnabled=0** → Flag désactivé en NVS (PISTE 1 confirmée)

### 3. Amélioration du diagnostic périodique (`src/automatism/automatism_sync.cpp`)

**Ajout:**
- Affichage de `RecvEnabled` dans le diagnostic toutes les 30 secondes

**Message attendu toutes les 30 secondes:**
```
[Sync] Diagnostic: WiFi=OK, SendEnabled=YES, RecvEnabled=YES, TimeSinceLastSend=XXXX ms, Interval=120000 ms, Ready=YES/NO
```

### 4. Log de debug dans `Automatism::update()` (`src/automatism.cpp`)

**Ajout:**
- Log toutes les 60 secondes pour confirmer l'appel à `_network.update()`

**Message attendu toutes les 60 secondes:**
```
[Auto] DEBUG PISTE 1: Appel _network.update() à XXXXX ms
```

---

## 🔬 Comment Vérifier la PISTE 1

### Étape 1: Capturer un nouveau log depuis le boot

1. **Redémarrer l'ESP32**
2. **Capturer tous les messages depuis le démarrage**
3. **Chercher les messages suivants:**

#### Messages de chargement (au boot):
```
[Config] 📥 Chargement flags réseau depuis NVS centralisé
[Config] ✅ Net flags chargés - send:XX recv:XX
[Config] 🔍 Diagnostic PISTE 1 - send: ...
```

**Interprétation:**
- Si `send:OFF` ou `recv:OFF` → **PISTE 1 CONFIRMÉE** (flags désactivés en NVS)
- Si `send:ON` et `recv:ON` → PISTE 1 non confirmée, vérifier PISTE 2

#### Messages de premier appel:
```
[Sync] ⚠️ DEBUG PISTE 1: update() appelé pour la première fois
[Sync] DEBUG PISTE 1: WiFi=X, SendEnabled=X, RecvEnabled=X
```

**Interprétation:**
- Si `SendEnabled=0` → **PISTE 1 CONFIRMÉE**
- Si `SendEnabled=1` mais aucun envoi → Vérifier PISTE 2 ou autres conditions

### Étape 2: Analyser les diagnostics périodiques

**Chercher dans le log:**
```
[Sync] Diagnostic: WiFi=OK, SendEnabled=YES/NO, RecvEnabled=YES/NO, ...
```

**Interprétation:**
- Si `SendEnabled=NO` → **PISTE 1 CONFIRMÉE**
- Si `SendEnabled=YES` mais `Ready=NO` → Vérifier l'intervalle ou autres conditions

---

## ✅ Résultats Attendus

### Scénario 1: PISTE 1 CONFIRMÉE (flags désactivés)

**Logs observés:**
```
[Config] ⚠️ ATTENTION: Envoi distant DÉSACTIVÉ en NVS (net_send_en=false)
[Sync] DEBUG PISTE 1: WiFi=3, SendEnabled=0, RecvEnabled=0
[Sync] ⚠️ Envoi POST bloqué: WiFi=OK, SendEnabled=NO, RecvEnabled=NO
```

**Action:** Activer les flags via:
- Interface web locale (`/api/config`)
- Ou directement en NVS

### Scénario 2: PISTE 1 NON CONFIRMÉE (flags activés)

**Logs observés:**
```
[Config] ✅ Net flags chargés - send:ON recv:ON
[Sync] DEBUG PISTE 1: WiFi=3, SendEnabled=1, RecvEnabled=1
[Sync] Diagnostic: WiFi=OK, SendEnabled=YES, RecvEnabled=YES, ...
```

**Action:** Vérifier PISTE 2 (méthode non appelée) ou autres conditions

### Scénario 3: PISTE 2 CONFIRMÉE (méthode non appelée)

**Logs observés:**
```
[Config] ✅ Net flags chargés - send:ON recv:ON
# MAIS AUCUN message [Sync] DEBUG PISTE 1
```

**Action:** Vérifier pourquoi `AutomatismSync::update()` n'est pas appelé

---

## 🛠️ Commandes pour Vérifier la NVS

### Via interface web (recommandé)

1. Accéder à `http://<IP_ESP32>/api/status`
2. Vérifier les champs:
   ```json
   {
     "sendEnabled": true/false,
     "recvEnabled": true/false
   }
   ```

### Via API pour activer

```bash
# Activer l'envoi
curl -X POST http://<IP_ESP32>/api/config \
  -d "net_send_en=1"

# Activer la réception
curl -X POST http://<IP_ESP32>/api/config \
  -d "net_recv_en=1"
```

---

## 📊 Checklist de Vérification

- [ ] Nouveau log capturé depuis le boot complet
- [ ] Messages `[Config] 📥 Chargement flags réseau` présents
- [ ] Messages `[Sync] DEBUG PISTE 1` présents
- [ ] Valeurs `SendEnabled` et `RecvEnabled` vérifiées
- [ ] Messages `[Sync] Diagnostic` présents toutes les 30s
- [ ] Messages `[Auto] DEBUG PISTE 1` présents toutes les 60s

---

## 🎯 Prochaines Étapes

1. **Compiler et flasher** avec les nouveaux logs
2. **Capturer un nouveau log** depuis le boot complet
3. **Analyser les messages** selon ce document
4. **Confirmer ou infirmer** la PISTE 1
5. **Agir en conséquence** :
   - Si PISTE 1 confirmée → Activer les flags
   - Si PISTE 1 non confirmée → Vérifier PISTE 2

---

**Modifications effectuées le:** 2026-01-25  
**Fichiers modifiés:**
- `src/config.cpp`
- `src/automatism/automatism_sync.cpp`
- `src/automatism.cpp`

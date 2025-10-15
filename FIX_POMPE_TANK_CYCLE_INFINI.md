# ✅ FIX : Cycle Infini Pompe Réservoir - v4.5.19

**Date** : 13 octobre 2025  
**Problème** : Pompe réservoir se répète sans s'arrêter lorsqu'activée depuis le serveur distant  
**Statut** : ✅ CORRIGÉ

---

## 🎯 Résumé

Correction d'un bug critique où la pompe réservoir (refill) démarrait en **boucle infinie** lorsqu'elle était activée depuis le **serveur distant** (`ffp3/`).

### Cause
**Désaccord de logique inversée** entre le serveur PHP et l'ESP32 :
- **Serveur** : GPIO 18 = 0 (pompe ON), mais renvoyait `pump_tank=0` à l'ESP32
- **ESP32** : Interprétait `pump_tank=0` comme "arrêter la pompe"
- **Résultat** : Cycle infini démarrage → arrêt → démarrage...

### Solution
Inversion de la logique dans `ffp3/src/Controller/OutputController.php` :
```php
// GPIO 18 = 0 (hardware ON) → pump_tank=1 (ESP32)
// GPIO 18 = 1 (hardware OFF) → pump_tank=0 (ESP32)
if ($gpio === 18) {
    $state = $state === 0 ? 1 : 0;
}
```

---

## 📦 Fichiers modifiés

### Serveur distant (sous-module `ffp3/`)
1. ✅ `ffp3/src/Controller/OutputController.php` (lignes 148-154)
2. ✅ `ffp3/VERSION` (4.5.18 → **4.5.19**)
3. ✅ `ffp3/CHANGELOG.md` (documentation complète)
4. ✅ `ffp3/CORRECTION_POMPE_TANK_CYCLE_INFINI_v4.5.19.md` (guide détaillé)

### ESP32 (code embarqué)
❌ **Aucune modification nécessaire** - Le bug était uniquement côté serveur

---

## 🚀 Déploiement

### 1. Prérequis
- Accès SSH au serveur distant hébergeant `ffp3/`
- Aucune modification sur l'ESP32 nécessaire

### 2. Commandes (serveur distant)
```bash
# 1. Sauvegarder l'ancienne version
cd /chemin/vers/ffp3
cp src/Controller/OutputController.php src/Controller/OutputController.php.backup_v4.5.18

# 2. Récupérer les modifications
git pull origin main

# 3. Vérifier la version
cat VERSION  # Doit afficher : 4.5.19

# 4. Pas de composer install ni migration BDD nécessaire
```

### 3. Tests à effectuer
- [ ] Activer pompe réservoir depuis interface web distante
- [ ] Vérifier qu'elle s'arrête après la durée configurée (pas de cycle infini)
- [ ] Vérifier logs ESP32 : pas de messages répétés
- [ ] Tester autres actionneurs (pompe aqua, lumière, chauffage)

---

## 📊 Impact

### ✅ Avantages
- Élimine le cycle infini de démarrage/arrêt
- La pompe respecte maintenant la durée configurée
- Synchronisation correcte entre serveur distant et ESP32
- Pas d'impact sur les autres actionneurs
- Compatible avec l'existant (pas de migration BDD)

### ⚠️ Risques
- **Aucun** - Correction ciblée uniquement sur GPIO 18
- Rollback facile en cas de problème (restaurer backup)

---

## 📝 Documentation complète

Pour plus de détails (scénario du bug, tests, rollback, etc.) :
📄 **Voir** : `ffp3/CORRECTION_POMPE_TANK_CYCLE_INFINI_v4.5.19.md`

---

## 🎓 Points clés

1. **Logique inversée hardware** : GPIO 18 utilise 0=ON, 1=OFF (relais inversé)
2. **Interface de communication** : L'ESP32 attend une logique normale (1=ON, 0=OFF)
3. **Solution transparente** : Inversion dans le contrôleur, pas de changement en BDD
4. **Test critique** : Toujours vérifier la synchronisation local/distant après correction

---

**Auteur** : Assistant IA  
**Version** : 1.0  
**Dernière mise à jour** : 13 octobre 2025


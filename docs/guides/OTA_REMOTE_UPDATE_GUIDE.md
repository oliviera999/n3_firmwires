# Guide de Mise à Jour OTA à Distance - FFP3CS4

## Nouveautés (>= 9.98)

### OLED enrichi pendant l'OTA

Le firmware affiche désormais des informations détaillées sur l'OLED via `showOtaProgressEx` :
- Phase (Checking, Téléchargement, Terminé, Erreur)
- Hôte (SSID ou AP)
- Version courante et nouvelle version
- Partitions From/To
- Barre de progression + pourcentage

Ces écrans sont mis à jour depuis les callbacks OTA (statut, progression, erreur) et juste avant le reboot à 100%.

### Rappels de configuration
- Vérification automatique toutes les 2h
- Espace & taille vérifiés avant MAJ
- Validation d'image et annulation de rollback après boot
- Emails envoyés en début et fin (serveur distant / interface web)

## Utilisation côté code

```cpp
// Mise à jour OLED depuis les callbacks
const char* curV = Config::VERSION;
const char* newV = otaManager.getRemoteVersion().c_str();
String host = WiFi.isConnected() ? WiFi.SSID() : WiFi.softAPSSID();
oled.showOtaProgressEx(progress, fromLbl, toLbl, phase, curV, newV, host.c_str());
```

## Logs attendus (extrait)
```
[OTA] ✅ OTA RÉACTIVÉ - Gestionnaire moderne et stable
[OTA] 🔎 Sélection OTA: env=test, model=esp32-wroom
[OTA] ✅ Aucune mise à jour disponible
```

## Bonnes pratiques
- Laisser l'OTA prioritaire: les tâches capteurs/affichage ralentissent pendant la MAJ.
- Surveiller la progression sur l'OLED et via le port série.
- Vérifier les emails automatiques en fin de MAJ.
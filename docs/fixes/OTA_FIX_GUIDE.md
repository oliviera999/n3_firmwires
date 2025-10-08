# Guide de résolution OTA - FFP3CS4

## Problème identifié

L'OTA ne fonctionne pas car la version dans le metadata.json du serveur (8.0) est identique à la version actuelle du firmware (8.0).

## Solutions

### ✅ Solution 1: Mettre à jour le metadata.json sur le serveur

1. **Accéder au serveur** `http://iot.olution.info/ffp3/ota/metadata.json`
2. **Modifier la version** de `8.0` vers `8.1` ou supérieure
3. **Sauvegarder** le fichier

### ✅ Solution 2: Vérifier le fichier firmware.bin

1. **Vérifier que le firmware.bin** correspond à la nouvelle version
2. **Télécharger le nouveau firmware** sur le serveur si nécessaire
3. **Vérifier l'accessibilité** du fichier

### ✅ Solution 3: Redémarrer l'ESP32

Après avoir mis à jour le metadata.json :
1. **Redémarrer l'ESP32**
2. **Surveiller les logs série** pour voir les messages OTA
3. **Vérifier que l'OTA se déclenche**

## Tests effectués

### ✅ Serveur OTA fonctionnel
- Métadonnées accessibles (HTTP 200)
- Firmware accessible (1.7 MB)
- Téléchargement possible

### ✅ Code OTA fonctionnel
- Fonction `checkForOtaUpdate()` correcte
- Comparaison de versions sémantique
- Gestion des erreurs appropriée

## Logs attendus

Quand l'OTA fonctionne, vous devriez voir :

```
[OTA] Début de la vérification des mises à jour...
[OTA] Nouvelle version 8.1 trouvée (courante 8.0)
[OTA] Téléchargement en cours...
[OTA] Mise à jour réussie, reboot...
```

## Debugging ajouté

J'ai ajouté des logs de debug dans `src/app.cpp` :

```cpp
Serial.println("[OTA] Début de la vérification des mises à jour...");
Serial.printf("[OTA DEBUG] Résultat de la comparaison: %d\n", versionComparison);
```

## Scripts de test

- `test_ota_server.py` : Teste l'accessibilité du serveur
- `test_force_ota_check.py` : Simule différents scénarios de versions

## Prochaines étapes

1. **Mettre à jour le metadata.json** sur le serveur vers la version 8.1
2. **Redémarrer l'ESP32**
3. **Surveiller les logs série**
4. **Vérifier que l'OTA se déclenche automatiquement**

## Vérification

Pour vérifier que l'OTA fonctionne :

```bash
python test_ota_server.py
```

Vous devriez voir :
```
Version distante: 8.1
✅ Nouvelle version disponible!
```

## Support

Si l'OTA ne fonctionne toujours pas après ces étapes :
1. Vérifier la connectivité WiFi
2. Vérifier les logs série détaillés
3. Tester manuellement l'URL du metadata.json
4. Vérifier les permissions du serveur web 
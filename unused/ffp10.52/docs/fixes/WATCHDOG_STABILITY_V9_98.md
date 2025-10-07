# Stabilité et robustesse réseau/OTA – v9.98

Cette note recense les améliorations de stabilité introduites en v9.98.

## Réseau / HTTP
- Succès HTTP désormais strict: seules les réponses 2xx–3xx sont considérées comme réussies (cf. `src/web_client.cpp`).
- Journalisation HTTP bornée: payload et réponse tronqués à ~300 caractères pour éviter la surcharge RAM/serial.
- WiFi: gestion manuelle de la reconnexion (`WiFi.persistent(false)`, `WiFi.setAutoReconnect(false)`) pour éviter des états bloquants.
- Sommeil WiFi: activé au repos, désactivé pendant les transferts (fiabilise les POST).
- Hostname unique par appareil (format `ffp3-XXXX`) utilisé pour DHCP/DNS et ArduinoOTA.

## OTA
- Callbacks enrichis affichant versions/partitions/hôte sur l’OLED (`showOtaProgressEx`).
- Validation de l’image au boot et annulation de rollback si nécessaire.
- Suspension des tâches capteurs/heartbeat pendant OTA pour prioriser le réseau.

## Watchdog / Tâches
- WDT natif configuré à 300s, resets fréquents dans tâches longues.
- Marges de stack (high water marks) capturées: log au boot et incluses dans le digest email périodique.

## Emails
- Sujets enrichis avec le hostname unique pour faciliter l’identification multi-appareils.
- Corps des emails (boot/OTA/digest) complétés avec hostname et contenu borné (5–6KB max) pour limiter la fragmentation.

## Sécurité Pompes (Automatismes)
- Nouvelle sécurité « aquarium trop plein »: verrouillage pompe réservoir si l’aquarium est au-dessus du seuil.
- Sécurité « réserve trop basse » (anti-marche à sec) clarifiée: libellés et emails adaptés.
- Messages de déverrouillage et objets de mails mis à jour.

## Impact
- Moins de blocages réseau, meilleure résilience OTA, meilleure visibilité sur la mémoire (stacks) et identification claire par hostname.

## Fichiers modifiés (principaux)
- `src/web_client.cpp`: stricte interprétation HTTP, troncatures logs.
- `src/wifi_manager.cpp`: reconnexion manuelle et stratégies sleep.
- `src/app.cpp`: hostname unique, sujets/corps d’emails, HWM stacks, digest borné.
- `src/automatism.cpp`: sécurités pompes améliorées et emails.



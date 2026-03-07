#pragma once
#include <Arduino.h>

struct N3OtaConfig {
    const char* metadataUrl;
    const char* currentVersion;
    int ledPin;           // GPIO LED de feedback (-1 = désactivé)
};

// Vérifie si une mise à jour OTA est disponible sur le serveur.
// Compare la version distante (champ "version" du JSON) avec currentVersion.
// Si une MAJ est disponible, télécharge le binaire et redémarre.
// Retourne true si une MAJ a été tentée (succès = reboot automatique).
// En cas d'échec ou si déjà à jour, retourne false et le firmware continue.
bool n3OtaCheck(const N3OtaConfig& config);

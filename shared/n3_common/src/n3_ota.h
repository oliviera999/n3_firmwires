#pragma once
#include <Arduino.h>

struct N3OtaConfig {
    const char* metadataUrl;
    const char* currentVersion;
    int ledPin;            // GPIO LED de feedback (-1 = désactivé), ou reserved (-1)
    const char* metadataKey; // NULL = objet unique {version,url,md5} ; sinon clé pour JSON multi-cible (ex. "msp1")
};

// Synchronise la partition de boot avec la partition en cours d'exécution.
// À appeler au tout début du setup() pour éviter que flash USB et OTA ne se trompent de partition.
void n3OtaSyncBootPartition();

// Vérifie si une mise à jour OTA est disponible sur le serveur.
// Compare la version distante (champ "version" du JSON) avec currentVersion.
// Si une MAJ est disponible, télécharge le binaire et redémarre.
// Retourne true si une MAJ a été tentée (succès = reboot automatique).
// En cas d'échec ou si déjà à jour, retourne false et le firmware continue.
bool n3OtaCheck(const N3OtaConfig& config);

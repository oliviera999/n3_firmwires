#ifndef CAMERA_SYNC_H
#define CAMERA_SYNC_H

#include <Arduino.h>

/*
 * Synchronisation hors-ligne du backlog photo (uploadphotosserver).
 *
 * Principe « renforcement offline » :
 *   - chaque photo capturée est numérotée et écrite sur la carte SD (file d'attente locale) ;
 *   - un curseur NVS mémorise le numéro de la dernière photo confirmée côté serveur ;
 *   - au retour du WiFi, on envoie les photos manquantes (numéro > curseur) selon une stratégie
 *     HYBRIDE : drain incrémental (quelques photos/réveil) par défaut, vidage complet si le
 *     backlog dépasse un seuil (rattrapage).
 *
 * Le transfert est encadré par une « session de sync » côté serveur (start / finish) qui
 * alimente l'indicateur de connexion, la jauge X/Y et le mail récapitulatif.
 *
 * Composant interne à uploadphotosserver (comme les modules pgl_*), sans état global autre que
 * les compteurs NVS (namespace "upcam").
 */

struct CameraSyncResult {
  bool ran;          // true si une session de sync a été tentée ce réveil
  uint32_t pending;  // taille du backlog au démarrage
  uint32_t planned;  // nombre de photos prévues ce réveil (selon la stratégie hybride)
  uint32_t sent;     // photos envoyées avec succès
  uint32_t failed;   // échecs
  uint32_t bytes;    // volume total envoyé
  int sessionId;     // identifiant serveur de session (>0 si ouverte)
  bool complete;     // true si le lot prévu a été entièrement transféré
};

struct CameraSyncConfig {
  const char* uploadUrl;        // URL upload.php
  const char* startUrl;         // URL .../sync/start
  const char* finishUrl;        // URL .../sync/finish
  const char* apiKey;           // clé API device
  int board;                    // identifiant board attendu côté serveur
  const char* targetName;       // msp1 / n3pp / ffp3
  const char* firmwareVersion;  // version firmware (rapportée au serveur)
  uint32_t maxUploadsPerWake;   // plafond drain incrémental
  uint32_t fullDrainThreshold;  // backlog au-delà duquel on vide tout ce réveil
  bool (*reconnect)();          // callback reconnexion WiFi (optionnel)
};

/** Numéro de la dernière photo écrite sur SD (compteur NVS). */
uint32_t cameraSyncWrittenCount();

/** Réserve et retourne le prochain numéro de photo (incrémente le compteur NVS). */
uint32_t cameraSyncNextPictureNumber();

/**
 * Construit le chemin SD d'une photo : "/<N sur 10 chiffres>_<stamp>.jpg".
 * N en tête => classement robuste. stamp = heure de capture "Y-m-d_H-i-s" (ou nullptr/""/"0" si
 * horloge inconnue, auquel cas le segment vaut "0"). Voir aussi le parsing dans cameraSyncDrain.
 */
String cameraSyncBuildSdPath(uint32_t n, const char* stamp);

/** Nombre de photos en attente d'upload (écrites − déjà confirmées). */
uint32_t cameraSyncPendingCount();

/** Draine le backlog SD selon la stratégie hybride. Retourne le bilan du réveil. */
CameraSyncResult cameraSyncDrain(const CameraSyncConfig& cfg);

#endif  // CAMERA_SYNC_H

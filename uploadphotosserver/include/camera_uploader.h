#ifndef CAMERA_UPLOADER_H
#define CAMERA_UPLOADER_H

#include <Arduino.h>

/*
 * Upload réutilisable d'un JPEG vers le serveur galerie (upload.php), en multipart/form-data
 * (champ "imageFile", même contrat que l'historique). Mutualise la logique HTTP entre :
 *   - la capture « live » en mémoire (capturePhoto) ;
 *   - le vidage du backlog stocké sur carte SD (camera_sync).
 *
 * Composant interne à uploadphotosserver pour l'instant (vocation à rejoindre shared/ plus tard,
 * comme les modules pgl_*). Aucun état global : tout passe par CameraUploadParams.
 */
struct CameraUploadParams {
  const char* url;          // URL complète d'upload (SERVER_SCHEME + host + upload.php)
  const char* apiKey;       // clé API device (en-tête X-Api-Key) ; nullptr = pas d'auth
  const char* syncSession;  // identifiant de session de sync (en-tête X-Sync-Session) ; nullptr/"" = hors session
  const char* capturedAt;   // heure de capture "Y-m-d_H-i-s" (en-tête X-Captured-At) ; nullptr/"" = absente
  const char* captureSeq;   // compteur de capture (en-tête X-Capture-Seq) ; nullptr/"" = absent
  const char* sigSecret;    // secret HMAC (API_SIG_SECRET) ; nullptr/"" = pas de signature (A4). Additif :
                            //   si défini + horloge valide -> en-têtes X-Sig-Timestamp/Nonce/Hmac
                            //   signant HMAC-SHA256(timestamp\n nonce\n api_key) (condensé stable).
  bool (*reconnect)();      // callback de reconnexion WiFi entre 2 tentatives ; nullptr = aucun
};

/**
 * Envoie un JPEG déjà présent en mémoire. Retourne le code HTTP (>0 OK, <0 erreur réseau).
 */
int cameraUploadJpegBuffer(const CameraUploadParams& params, const uint8_t* image, size_t imageLen, const String& filename);

/** Erreur locale SD (fichier absent / illisible / 0 octet) — distincte des codes HTTPClient ≤ 0. */
constexpr int CAMERA_UPLOAD_LOCAL_ERROR = -100;

/**
 * Envoie un JPEG stocké sur la carte SD (lu en RAM — PSRAM si dispo — puis posté).
 * outBytes (optionnel) reçoit la taille envoyée en cas de succès.
 * Retourne le code HTTP (>0), {@see CAMERA_UPLOAD_LOCAL_ERROR} si le fichier SD est inutilisable,
 * ou un code HTTPClient ≤ 0 pour une erreur réseau.
 */
int cameraUploadJpegFile(const CameraUploadParams& params, const String& sdPath, const String& filename, size_t* outBytes);

#endif  // CAMERA_UPLOADER_H

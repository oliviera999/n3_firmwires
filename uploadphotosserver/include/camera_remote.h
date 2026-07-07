#ifndef CAMERA_REMOTE_H
#define CAMERA_REMOTE_H

#include <Arduino.h>
#include "n3_notify.h"  // Phase 3 : taxonomie N3Severity/N3NotifMode (lib n3_mail)

struct CameraRemoteConfig {
  String mail;
  /* Retro-compat : true si mode != None (anciens consommateurs booleen). */
  bool mailNotif;
  /* Phase 3 arbitrage : mode de notification gradue derive de la cle 103.
   * Retro-compatible : bool/"1"/"checked" -> Full ; "0"/"unchecked" -> None ;
   * nouvelles valeurs none|important|partial|full acceptees. */
  N3NotifMode notifMode;
  bool forceWakeUp;
  uint32_t sleepTimeSeconds;
  bool resetMode;
};

bool cameraRemoteFetchConfig(CameraRemoteConfig& outConfig, unsigned int* outHttpCode);
int cameraRemotePostFirmwareVersion(const char* targetName);

#endif /* CAMERA_REMOTE_H */


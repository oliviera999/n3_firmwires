#ifndef CAMERA_REMOTE_H
#define CAMERA_REMOTE_H

#include <Arduino.h>

struct CameraRemoteConfig {
  String mail;
  bool mailNotif;
  bool forceWakeUp;
  uint32_t sleepTimeSeconds;
  bool resetMode;
};

bool cameraRemoteFetchConfig(CameraRemoteConfig& outConfig, unsigned int* outHttpCode);
int cameraRemotePostFirmwareVersion(const char* targetName);

#endif /* CAMERA_REMOTE_H */


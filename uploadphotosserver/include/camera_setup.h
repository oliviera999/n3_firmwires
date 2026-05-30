#ifndef CAMERA_SETUP_H
#define CAMERA_SETUP_H

#include <stdbool.h>
#include "config.h"

bool n3CameraUseHighResBuffers();
void n3LogHardwareDiagnostics();
#if USE_DEEP_SLEEP
void adjustExposure();
void warmupCamera();
void initializeCamera();
#endif

#endif

#ifndef CAMERA_SETUP_H
#define CAMERA_SETUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"
#include "esp_camera.h"
#include "esp_err.h"

bool n3CameraSpiramHeapPresent(void);
bool n3CameraUseHighResBuffers(void);
void n3LogHardwareDiagnostics(void);
/** Sonde SCCB (GPIO26/27) avant esp_camera_init : bus I2C, PID OV2640 attendu 0x2642. */
void n3LogCameraSccbDiagnostics(void);
/** Essaie SXGA/CIF en PSRAM puis SVGA/CIF/QQVGA en DRAM ; deinit entre chaque tentative. */
esp_err_t n3CameraInitWithFallback(camera_config_t* config, char* activeModeLabel,
                                   size_t activeModeLabelLen, uint8_t* outAttemptCount = nullptr);
/** Coupe le capteur (deinit + PWDN) avant tout trafic réseau ; sans effet si déjà coupé. */
void n3CameraPowerDown(void);
/** Capture une trame JPEG ; si fb_get rend nul, retente en dégradant la qualité
 *  (CAM_CAPTURE_MAX_ATTEMPTS essais). nullptr si tous échouent : failReason décrit l'échec. */
camera_fb_t* n3CameraCaptureJpeg(char* failReason, size_t failReasonLen);
#if USE_DEEP_SLEEP
void warmupCamera();
void initializeCamera();
#endif

#endif

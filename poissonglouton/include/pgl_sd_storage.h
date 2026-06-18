#pragma once

/** Monte la carte SD une seule fois (partagee audio + journal offline). */
bool pglSdStorageBegin();

/** true si la SD est montee et utilisable. */
bool pglSdStorageIsMounted();

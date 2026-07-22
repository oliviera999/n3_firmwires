#pragma once

#include <stdint.h>

enum class N3OtaDownloadTimeout : uint8_t {
    None,
    Idle,
    Total,
};

/**
 * Décision pure de timeout OTA, compatible avec le rebouclage de millis().
 */
inline N3OtaDownloadTimeout n3OtaDownloadTimeout(
    uint32_t now,
    uint32_t startedAt,
    uint32_t lastProgressAt,
    uint32_t idleTimeoutMs,
    uint32_t maxDurationMs) {
    if ((now - startedAt) >= maxDurationMs) {
        return N3OtaDownloadTimeout::Total;
    }
    if ((now - lastProgressAt) >= idleTimeoutMs) {
        return N3OtaDownloadTimeout::Idle;
    }
    return N3OtaDownloadTimeout::None;
}

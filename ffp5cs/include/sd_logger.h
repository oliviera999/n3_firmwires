#pragma once

/**
 * @file sd_logger.h
 * @brief Stockage des POSTs sur carte SD (BOARD_S3 uniquement).
 *
 * Deux rôles :
 *  1. Archive journalière (log/YYYYMMDD.csv) pour graphiques locaux.
 *  2. File d'attente persistante (queue/SEQNUM.dat) pour replay offline.
 *
 * Si la carte SD est absente, toutes les fonctions retournent false/0
 * et le firmware continue avec les queues RAM/NVS existantes.
 */

#include <cstdint>
#include <cstddef>

namespace SdLogger {

struct QueueResult {
  bool ok;
  uint32_t seqNum;
};

bool begin();

QueueResult logAndQueue(const char* payload, uint32_t epochSec);

bool markSent(uint32_t seqNum);

uint16_t replayPending(uint8_t maxBatch = 5);

uint16_t pendingCount();

/** Écrit les entrées du fichier log du jour dans outBuf (JSON array tronqué si nécessaire).
 *  date au format "YYYYMMDD". offset = nombre de lignes à sauter. */
bool readHistory(const char* date, char* outBuf, size_t bufSize, uint32_t offset = 0, uint32_t limit = 50);

void rotateLogs(uint16_t keepDays = 30);

}  // namespace SdLogger

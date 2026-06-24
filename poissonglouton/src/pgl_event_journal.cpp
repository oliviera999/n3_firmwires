#include "pgl_event_journal.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>

#include <time.h>

#include "config.h"
#include "pgl_log.h"
#include "pgl_sd_storage.h"

uint16_t PglEventJournal::crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) : (crc << 1);
    }
  }
  return crc;
}

void PglEventJournal::loadMeta() {
  Preferences p;
  p.begin("pgljrn", true);
  writeOffset_ = p.getULong("woff", 0);
  ackOffset_ = p.getULong("aoff", 0);
  p.end();
}

void PglEventJournal::saveMeta() const {
  Preferences p;
  p.begin("pgljrn", false);
  p.putULong("woff", writeOffset_);
  p.putULong("aoff", ackOffset_);
  p.end();
}

bool PglEventJournal::canRead() const {
  return pglSdStorageIsMounted() && writeOffset_ > ackOffset_;
}

bool PglEventJournal::tryRecoverWrite() {
  if (sdOk_) return true;
  if (!pglSdStorageIsMounted() && !pglSdStorageBegin()) {
    return false;
  }
  sdOk_ = true;
  PGL_LOG("Journal SD: ecriture retablie apres panne transitoire");
  return true;
}

void PglEventJournal::begin() {
  loadMeta();

  if (!pglSdStorageIsMounted() && !pglSdStorageBegin()) {
    if (writeOffset_ > ackOffset_) {
      PGL_LOG("Journal SD: carte absente mais meta pending=%lu — reprise a la monture",
              static_cast<unsigned long>(pendingCount()));
    } else {
      PGL_LOG("Journal SD: carte absente ou HS — mode degrade (NVS FIFO)");
    }
    sdOk_ = false;
    return;
  }

  sdOk_ = true;

  const size_t fileSize = SD.exists(PGL_JOURNAL_PATH)
                              ? static_cast<size_t>(SD.open(PGL_JOURNAL_PATH).size())
                              : 0;

  if (writeOffset_ > static_cast<uint32_t>(fileSize)) {
    PGL_LOG("Journal SD: writeOffset=%lu > taille fichier=%u — resync",
            static_cast<unsigned long>(writeOffset_),
            static_cast<unsigned int>(fileSize));
    writeOffset_ = static_cast<uint32_t>(
        (fileSize / RECORD_SIZE) * RECORD_SIZE);
    if (ackOffset_ > writeOffset_) ackOffset_ = writeOffset_;
    saveMeta();
  }

  PGL_LOG("Journal SD: OK woff=%lu aoff=%lu pending~%lu records",
          static_cast<unsigned long>(writeOffset_),
          static_cast<unsigned long>(ackOffset_),
          static_cast<unsigned long>(pendingCount()));
}

bool PglEventJournal::writeRecord(const JournalRecord& rec) {
  File f = SD.open(PGL_JOURNAL_PATH, FILE_APPEND);
  if (!f) {
    PGL_LOG("Journal SD: impossible d'ouvrir %s en ecriture", PGL_JOURNAL_PATH);
    sdOk_ = false;
    return false;
  }

  uint8_t buf[RECORD_SIZE];
  memcpy(buf, &rec, sizeof(JournalRecord));
  const uint16_t crc = crc16(buf, sizeof(JournalRecord));
  buf[sizeof(JournalRecord)] = static_cast<uint8_t>(crc >> 8);
  buf[sizeof(JournalRecord) + 1] = static_cast<uint8_t>(crc & 0xFF);

  const size_t written = f.write(buf, RECORD_SIZE);
  f.close();

  if (written != RECORD_SIZE) {
    PGL_LOG("Journal SD: ecriture incomplete (%u/%u)", written, RECORD_SIZE);
    sdOk_ = false;
    return false;
  }
  return true;
}

bool PglEventJournal::readRecord(uint32_t offset, JournalRecord& out) const {
  if (!pglSdStorageIsMounted()) return false;

  File f = SD.open(PGL_JOURNAL_PATH, FILE_READ);
  if (!f) return false;

  if (!f.seek(offset)) {
    f.close();
    return false;
  }

  uint8_t buf[RECORD_SIZE];
  const size_t n = f.read(buf, RECORD_SIZE);
  f.close();

  if (n != RECORD_SIZE) return false;

  const uint16_t expected = crc16(buf, sizeof(JournalRecord));
  const uint16_t stored =
      (static_cast<uint16_t>(buf[sizeof(JournalRecord)]) << 8) |
      buf[sizeof(JournalRecord) + 1];

  if (expected != stored) {
    PGL_LOG("Journal SD: CRC invalide offset=%lu (exp=%04X got=%04X)",
            static_cast<unsigned long>(offset), expected, stored);
    return false;
  }

  memcpy(&out, buf, sizeof(JournalRecord));
  return true;
}

bool PglEventJournal::append(const PglStoredEvent& event) {
  if (!tryRecoverWrite()) return false;

  JournalRecord rec{};
  rec.eventId = event.eventId;
  rec.epoch = event.epoch;
  rec.countDelta = event.countDelta;
  rec.sensorMode = event.sensorMode;
  rec.tandemValidated = event.tandemValidated;
  rec.batteryMilliVolt = event.batteryMilliVolt;
  rec.rssi = event.rssi;

  if (!writeRecord(rec)) return false;

  writeOffset_ += RECORD_SIZE;
  saveMeta();
  return true;
}

bool PglEventJournal::decodeRecord(const uint8_t* buf, JournalRecord& out) {
  const uint16_t expected = crc16(buf, sizeof(JournalRecord));
  const uint16_t stored =
      (static_cast<uint16_t>(buf[sizeof(JournalRecord)]) << 8) |
      buf[sizeof(JournalRecord) + 1];
  if (expected != stored) return false;
  memcpy(&out, buf, sizeof(JournalRecord));
  return true;
}

size_t PglEventJournal::readPending(PglStoredEvent* out, size_t maxItems,
                                    uint32_t& outFirstId,
                                    uint32_t& outLastId) const {
  if (!canRead() || !out || maxItems == 0) return 0;

  File f = SD.open(PGL_JOURNAL_PATH, FILE_READ);
  if (!f || !f.seek(ackOffset_)) {
    if (f) f.close();
    return 0;
  }

  size_t count = 0;
  uint32_t offset = ackOffset_;
  uint8_t corruptStreak = 0;

  while (count < maxItems && offset < writeOffset_) {
    uint8_t buf[RECORD_SIZE];
    const size_t rd = f.read(buf, RECORD_SIZE);
    if (rd != RECORD_SIZE) break;  // EOF/troncature
    offset += RECORD_SIZE;

    JournalRecord rec{};
    if (!decodeRecord(buf, rec)) {
      if (++corruptStreak >= 5) {
        PGL_LOG("Journal SD: ALERTE %u records corrompus consecutifs a offset=%lu",
                corruptStreak,
                static_cast<unsigned long>(offset - RECORD_SIZE));
      }
      continue;
    }
    corruptStreak = 0;

    PglStoredEvent& ev = out[count];
    ev.eventId = rec.eventId;
    ev.epoch = rec.epoch;
    ev.countDelta = rec.countDelta;
    ev.sensorMode = rec.sensorMode;
    ev.tandemValidated = rec.tandemValidated;
    ev.batteryMilliVolt = rec.batteryMilliVolt;
    ev.rssi = rec.rssi;

    if (count == 0) outFirstId = rec.eventId;
    outLastId = rec.eventId;

    count++;
  }

  f.close();
  return count;
}

uint32_t PglEventJournal::findAckOffset(uint32_t lastAckedId) const {
  if (!canRead()) return ackOffset_;

  File f = SD.open(PGL_JOURNAL_PATH, FILE_READ);
  if (!f || !f.seek(ackOffset_)) {
    if (f) f.close();
    return ackOffset_;
  }

  uint32_t offset = ackOffset_;
  uint8_t corruptStreak = 0;
  uint32_t found = ackOffset_;
  while (offset < writeOffset_) {
    uint8_t buf[RECORD_SIZE];
    const size_t rd = f.read(buf, RECORD_SIZE);
    if (rd != RECORD_SIZE) break;
    offset += RECORD_SIZE;

    JournalRecord rec{};
    if (!decodeRecord(buf, rec)) {
      if (++corruptStreak >= 5) {
        PGL_LOG("Journal SD: findAckOffset bloque — %u corruptions consecutives",
                corruptStreak);
        f.close();
        return ackOffset_;
      }
      continue;
    }
    corruptStreak = 0;
    if (rec.eventId == lastAckedId) {
      found = offset;
      break;
    }
  }
  f.close();
  return found;
}

bool PglEventJournal::commitAck(uint32_t lastAckedId) {
  if (!canRead()) return false;

  const uint32_t newAck = findAckOffset(lastAckedId);
  if (newAck <= ackOffset_) return false;

  ackOffset_ = newAck;
  saveMeta();

  PGL_LOG("Journal SD: ack id=%lu newAckOffset=%lu pending~%lu",
          static_cast<unsigned long>(lastAckedId),
          static_cast<unsigned long>(ackOffset_),
          static_cast<unsigned long>(pendingCount()));

  if (sdOk_ && ackOffset_ >= PGL_JOURNAL_COMPACT_THRESHOLD) {
    compact();
  }
  return true;
}

void PglEventJournal::compact() {
  PGL_LOG("Journal SD: compaction (ackOffset=%lu)...",
          static_cast<unsigned long>(ackOffset_));

  if (SD.exists(PGL_JOURNAL_COMPACT_TMP)) {
    SD.remove(PGL_JOURNAL_COMPACT_TMP);
  }

  uint32_t readOff = ackOffset_;
  uint32_t newWriteOff = 0;
  bool ok = true;

  while (readOff < writeOffset_) {
    JournalRecord rec{};
    if (!readRecord(readOff, rec)) {
      readOff += RECORD_SIZE;
      continue;
    }
    readOff += RECORD_SIZE;

    File tmp = SD.open(PGL_JOURNAL_COMPACT_TMP, FILE_APPEND);
    if (!tmp) {
      ok = false;
      break;
    }

    uint8_t buf[RECORD_SIZE];
    memcpy(buf, &rec, sizeof(JournalRecord));
    const uint16_t crc = crc16(buf, sizeof(JournalRecord));
    buf[sizeof(JournalRecord)] = static_cast<uint8_t>(crc >> 8);
    buf[sizeof(JournalRecord) + 1] = static_cast<uint8_t>(crc & 0xFF);
    const size_t w = tmp.write(buf, RECORD_SIZE);
    tmp.close();

    if (w != RECORD_SIZE) {
      ok = false;
      break;
    }
    newWriteOff += RECORD_SIZE;
  }

  if (!ok || newWriteOff == 0) {
    PGL_LOG("Journal SD: compaction echouee — conservation du fichier original");
    SD.remove(PGL_JOURNAL_COMPACT_TMP);
    return;
  }

  if (SD.exists(PGL_JOURNAL_BACKUP_PATH)) {
    SD.remove(PGL_JOURNAL_BACKUP_PATH);
  }
  if (SD.exists(PGL_JOURNAL_PATH)) {
    if (!SD.rename(PGL_JOURNAL_PATH, PGL_JOURNAL_BACKUP_PATH)) {
      PGL_LOG("Journal SD: compaction echouee — backup impossible");
      SD.remove(PGL_JOURNAL_COMPACT_TMP);
      return;
    }
  }
  if (!SD.rename(PGL_JOURNAL_COMPACT_TMP, PGL_JOURNAL_PATH)) {
    PGL_LOG("Journal SD: compaction echouee — restauration backup");
    if (SD.exists(PGL_JOURNAL_BACKUP_PATH)) {
      SD.rename(PGL_JOURNAL_BACKUP_PATH, PGL_JOURNAL_PATH);
    }
    SD.remove(PGL_JOURNAL_COMPACT_TMP);
    return;
  }
  SD.remove(PGL_JOURNAL_BACKUP_PATH);

  writeOffset_ = newWriteOff;
  ackOffset_ = 0;
  saveMeta();

  PGL_LOG("Journal SD: compaction OK — nouveau fichier %lu records",
          static_cast<unsigned long>(newWriteOff / RECORD_SIZE));
}

uint32_t PglEventJournal::pendingCount() const {
  if (!canRead()) return 0;
  return (writeOffset_ - ackOffset_) / RECORD_SIZE;
}

uint32_t PglEventJournal::oldestPendingEpoch() const {
  if (!canRead()) return 0;
  JournalRecord rec{};
  if (!readRecord(ackOffset_, rec)) return 0;
  return rec.epoch;
}

uint32_t PglEventJournal::sumDeltasInRange(uint32_t fromOffset, uint32_t toOffset,
                                           uint16_t dayKey) const {
  if (!pglSdStorageIsMounted() || fromOffset >= toOffset) return 0;

  File f = SD.open(PGL_JOURNAL_PATH, FILE_READ);
  if (!f || !f.seek(fromOffset)) {
    if (f) f.close();
    return 0;
  }

  uint32_t sum = 0;
  uint32_t offset = fromOffset;
  while (offset < toOffset && offset < writeOffset_) {
    uint8_t recBuf[RECORD_SIZE];
    const size_t rd = f.read(recBuf, RECORD_SIZE);
    if (rd != RECORD_SIZE) break;
    offset += RECORD_SIZE;

    JournalRecord rec{};
    if (!decodeRecord(recBuf, rec)) {
      continue;
    }
    if (dayKey != 0) {
      if (rec.epoch < 1700000000) {
        continue;
      }
      time_t t = static_cast<time_t>(rec.epoch);
      struct tm tmNow = {};
      if (!localtime_r(&t, &tmNow)) {
        continue;
      }
      const uint16_t y = static_cast<uint16_t>((tmNow.tm_year + 1900) % 100);
      const uint16_t d = static_cast<uint16_t>(tmNow.tm_yday + 1);
      const uint16_t recKey = static_cast<uint16_t>((y * 400) + d);
      if (recKey != dayKey) {
        continue;
      }
    }
    sum += rec.countDelta;
  }
  f.close();
  return sum;
}

uint32_t PglEventJournal::fixInvalidEpochs(uint32_t nowEpoch) {
  // Backfill unique : NTP doit etre valide, la lecture possible et les
  // ecritures autorisees (il faut pouvoir reecrire en place sur la SD).
  if (nowEpoch < 1700000000 || !canRead() || !sdOk_) return 0;

  // Ouverture unique en lecture+ecriture. arduino-esp32 expose le mode
  // POSIX "r+" via le VFS (lecture + ecriture sans troncature).
  File f = SD.open(PGL_JOURNAL_PATH, "r+");
  if (!f) {
    PGL_LOG("Journal SD: fixInvalidEpochs — ouverture r+ impossible");
    return 0;
  }
  if (!f.seek(ackOffset_)) {
    f.close();
    return 0;
  }

  uint32_t fixedCount = 0;
  uint32_t offset = ackOffset_;

  while (offset < writeOffset_) {
    uint8_t buf[RECORD_SIZE];
    const size_t rd = f.read(buf, RECORD_SIZE);
    if (rd != RECORD_SIZE) break;  // EOF/troncature

    JournalRecord rec{};
    if (!decodeRecord(buf, rec)) {
      // Record corrompu : on saute en preservant l'alignement.
      offset += RECORD_SIZE;
      continue;
    }

    if (rec.epoch < 1700000000) {
      rec.epoch = nowEpoch;

      // Re-encodage complet : recopie de la struct entiere (reserved inclus)
      // puis CRC16 sur les 18 octets, 2 octets de CRC big-endian.
      uint8_t out[RECORD_SIZE];
      memcpy(out, &rec, sizeof(JournalRecord));
      const uint16_t crc = crc16(out, sizeof(JournalRecord));
      out[sizeof(JournalRecord)] = static_cast<uint8_t>(crc >> 8);
      out[sizeof(JournalRecord) + 1] = static_cast<uint8_t>(crc & 0xFF);

      if (!f.seek(offset)) break;  // repositionne sur le record courant
      const size_t w = f.write(out, RECORD_SIZE);
      if (w != RECORD_SIZE) {
        PGL_LOG("Journal SD: fixInvalidEpochs — reecriture incomplete offset=%lu",
                static_cast<unsigned long>(offset));
        break;
      }
      // Apres seek+write, repositionne le curseur sur le record suivant.
      if (!f.seek(offset + RECORD_SIZE)) break;
      fixedCount++;
    }

    offset += RECORD_SIZE;
  }

  f.flush();
  f.close();

  if (fixedCount > 0) {
    PGL_LOG("Journal SD: %lu epoch(s) corrige(s) apres sync NTP",
            static_cast<unsigned long>(fixedCount));
  }
  return fixedCount;
}

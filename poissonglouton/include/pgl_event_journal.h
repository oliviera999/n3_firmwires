#pragma once

#include <Arduino.h>

#include "pgl_types.h"

/**
 * Journal d'événements offline sur carte SD (append-only).
 *
 * Chaque record est une structure binaire fixe + CRC16 de 2 octets.
 * Deux curseurs NVS permettent de reprendre après reboot :
 *   - writeOffset : prochain octet à écrire (= taille logique du journal)
 *   - ackOffset   : tous les octets < ackOffset ont été confirmés par le serveur
 *
 * Quand ackOffset dépasse PGL_JOURNAL_COMPACT_THRESHOLD, une compaction
 * réécrit uniquement les records non encore acquittés dans un fichier
 * temporaire puis effectue un swap atomique.
 *
 * Mode dégradé : si la SD n'est pas disponible, sdOk_ == false.
 * La lecture du journal reste possible tant que la carte est montée
 * (reprise upload après panne d'écriture transitoire).
 */
class PglEventJournal {
 public:
  // Taille d'un record sur disque : PglJournalRecord + uint16_t CRC
  static constexpr size_t RECORD_SIZE = 20;

  void begin();

  /** Ajoute un événement dans le journal. Retourne false si SD HS. */
  bool append(const PglStoredEvent& event);

  /**
   * Lit au plus maxItems records non encore acquittés à partir du curseur
   * interne de lecture. Retourne le nombre de records lus.
   * outFirstId et outLastId reçoivent l'event_id du premier et du dernier.
   */
  size_t readPending(PglStoredEvent* out, size_t maxItems,
                     uint32_t& outFirstId, uint32_t& outLastId) const;

  /**
   * Marque tous les événements jusqu'à lastAckedId (inclus) comme acquittés.
   * Déclenche une compaction si le seuil est atteint.
   * Retourne true si le curseur ack a avancé.
   */
  bool commitAck(uint32_t lastAckedId);

  /** Nombre approximatif de records non acquittés (estimation par taille). */
  uint32_t pendingCount() const;

  /** epoch du record le plus ancien non acquitté (0 si journal vide/dégradé). */
  uint32_t oldestPendingEpoch() const;

  /** Somme des countDelta entre deux offsets [from, to). dayKey=0 : tous les jours. */
  uint32_t sumDeltasInRange(uint32_t fromOffset, uint32_t toOffset,
                            uint16_t dayKey = 0) const;

  /**
   * Corrige les epochs invalides (< 1700000000) des records non encore
   * acquittes [ackOffset_, writeOffset_) en les remplacant par nowEpoch,
   * reecrits en place (struct + CRC recalcule). A appeler une seule fois
   * quand NTP devient valide. Retourne le nombre de records corriges.
   */
  uint32_t fixInvalidEpochs(uint32_t nowEpoch);

  uint32_t getWriteOffset() const { return writeOffset_; }
  uint32_t getAckOffset() const { return ackOffset_; }

  /** Écritures journal autorisées (SD montée et dernière écriture OK). */
  bool isSdOk() const { return sdOk_; }

  /** Lecture du journal possible (SD montée, pending > 0). */
  bool canRead() const;

  /** Tente de rétablir les écritures après une panne transitoire. */
  bool tryRecoverWrite();

 private:
  // Acces en lecture seule aux internes (crc16/decodeRecord/JournalRecord) pour
  // les tests natifs Unity. Defini uniquement sous UNIT_TEST (cf.
  // test/test_journal_logic) ; aucun impact sur le firmware embarque.
#ifdef UNIT_TEST
  friend struct PglJournalTestAccess;
#endif

  struct JournalRecord {
    uint32_t eventId;
    uint32_t epoch;
    uint16_t countDelta;
    uint8_t sensorMode;
    uint8_t tandemValidated;
    int16_t batteryMilliVolt;
    int16_t rssi;
    uint8_t reserved[2] = {0, 0};  // format disque fixe 18 octets
  } __attribute__((packed));
  static_assert(sizeof(JournalRecord) == 18, "JournalRecord size mismatch");

  void loadMeta();
  void saveMeta() const;
  bool writeRecord(const JournalRecord& rec);
  bool readRecord(uint32_t offset, JournalRecord& out) const;
  void compact();
  uint32_t findAckOffset(uint32_t lastAckedId) const;

  static uint16_t crc16(const uint8_t* data, size_t len);
  // Valide le CRC d'un buffer record brut (RECORD_SIZE octets) et, si OK,
  // le decode dans `out`. Lecture sequentielle : pas d'I/O ici.
  static bool decodeRecord(const uint8_t* buf, JournalRecord& out);

  bool sdOk_ = false;
  uint32_t writeOffset_ = 0;  // octet de fin du journal (taille totale)
  uint32_t ackOffset_ = 0;    // premier octet non encore acquitté
};

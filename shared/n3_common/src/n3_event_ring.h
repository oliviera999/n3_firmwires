#pragma once
// =============================================================================
// n3_common — Journal de bord persistant : anneau d'événements (logique PURE)
// =============================================================================
// Anneau de N événements horodatés (séquence croissante, horloge firmware si plausible,
// uptime, code court, détail borné), sérialisable en JSON compact pour le serveur
// (`POST /ota-report` et champ `log=` du heartbeat, cf. n3_serveur docs/ENDPOINTS_ESP32_SERVEUR.md)
// et en « slots » texte pour la persistance NVS (un slot = une clé, ≤ 15 caractères de clé
// imposés par ESP-IDF, valeur ≤ kSlotChars).
//
// Pourquoi : incident du 06/11/2026 — un module s'est tu après une OTA sans qu'aucune trace
// (échec Wi-Fi, DNS/TLS, code HTTP, NTP, entrée/sortie de veille, panic) ne survive au reboot.
// Les événements sont acquittés par le serveur (séquences `acked`) ; tant qu'ils ne le sont pas,
// ils sont renvoyés au contact suivant.
//
// Aucune dépendance Arduino : testé en natif (ffp5cs/test/test_event_ring). La persistance
// (NVS / Preferences) est faite par l'appelant via encodeSlot() / decodeSlot().
// =============================================================================

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace n3evt {

constexpr uint8_t kDefaultCapacity = 16;
constexpr size_t kEventChars = 20;   // code court ("wifi_fail", "ota_start"...) + NUL
constexpr size_t kDetailChars = 48;  // détail borné + NUL (76 o par événement : budget DRAM WROOM)
constexpr size_t kSlotChars = 128;   // encodage texte d'un slot (seq|ts|uptime|event|detail)

struct Event {
  uint32_t seq;        // 0 = slot vide
  uint32_t ts;         // horloge firmware (epoch), 0 = inconnue / non plausible
  uint32_t uptimeS;    // uptime au moment de l'événement
  char event[kEventChars];
  char detail[kDetailChars];
};

template <uint8_t Capacity = kDefaultCapacity>
class Ring {
 public:
  static constexpr uint8_t kCapacity = Capacity;

  Ring() { clear(); }

  void clear() {
    memset(_slots, 0, sizeof(_slots));
    _head = 0;
    _nextSeq = 1;
  }

  // Ajoute un événement (écrase le plus ancien si plein). Retourne la séquence attribuée.
  uint32_t push(const char* event, const char* detail, uint32_t ts, uint32_t uptimeS) {
    Event& e = _slots[_head];
    e.seq = _nextSeq++;
    if (_nextSeq == 0) _nextSeq = 1;  // jamais 0 (= vide)
    e.ts = ts;
    e.uptimeS = uptimeS;
    copy(e.event, sizeof(e.event), event ? event : "?");
    copy(e.detail, sizeof(e.detail), detail ? detail : "");
    _head = static_cast<uint8_t>((_head + 1) % Capacity);
    return e.seq;
  }

  // Nombre d'événements non acquittés.
  uint8_t pendingCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < Capacity; ++i) if (_slots[i].seq != 0) ++n;
    return n;
  }

  // Séquence la plus élevée présente (0 si vide).
  uint32_t maxSeq() const {
    uint32_t m = 0;
    for (uint8_t i = 0; i < Capacity; ++i) if (_slots[i].seq > m) m = _slots[i].seq;
    return m;
  }

  // Acquitte (vide) tous les événements de séquence ≤ seq. Retourne le nombre vidé.
  uint8_t ackUpTo(uint32_t seq) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < Capacity; ++i) {
      if (_slots[i].seq != 0 && _slots[i].seq <= seq) { _slots[i].seq = 0; ++n; }
    }
    return n;
  }

  // Sérialise les événements non acquittés, du plus ancien au plus récent, en JSON compact :
  // [{"q":7,"t":1793981517,"u":30,"e":"ota_check","d":"remote=15.33"},...]
  // Retourne le nombre d'événements écrits ; buf toujours NUL-terminé ("[]" si vide).
  // Les événements qui ne tiennent pas sont omis (les plus récents d'abord omis) — le serveur
  // les recevra au contact suivant puisqu'ils restent non acquittés.
  uint8_t toJson(char* buf, size_t size, uint32_t* lastSeqWritten = nullptr) const {
    if (!buf || size < 3) { if (buf && size) buf[0] = '\0'; return 0; }
    size_t pos = 0;
    buf[pos++] = '[';
    uint8_t written = 0;
    uint32_t lastSeq = 0;
    // ordre chronologique : à partir de _head (plus ancien) sur Capacity slots
    for (uint8_t k = 0; k < Capacity; ++k) {
      const Event& e = _slots[(_head + k) % Capacity];
      if (e.seq == 0) continue;
      char item[kSlotChars + 48];
      char detail[kDetailChars * 2];
      escape(detail, sizeof(detail), e.detail);
      int n = snprintf(item, sizeof(item), "%s{\"q\":%lu,\"t\":%lu,\"u\":%lu,\"e\":\"%s\",\"d\":\"%s\"}",
                       written ? "," : "", (unsigned long)e.seq, (unsigned long)e.ts,
                       (unsigned long)e.uptimeS, e.event, detail);
      if (n < 0) continue;
      if (pos + (size_t)n + 2 > size) break;  // +2 : ']' + NUL
      memcpy(buf + pos, item, (size_t)n);
      pos += (size_t)n;
      ++written;
      lastSeq = e.seq;
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    if (lastSeqWritten) *lastSeqWritten = lastSeq;
    return written;
  }

  // Encodage texte d'un slot pour la persistance : "seq|ts|uptime|event|detail" ('|' interdit
  // dans event/detail : remplacé par '/'). Slot vide → chaîne vide.
  bool encodeSlot(uint8_t index, char* out, size_t size) const {
    if (index >= Capacity || !out || size == 0) return false;
    const Event& e = _slots[index];
    if (e.seq == 0) { out[0] = '\0'; return true; }
    char ev[kEventChars], de[kDetailChars];
    sanitize(ev, sizeof(ev), e.event);
    sanitize(de, sizeof(de), e.detail);
    int n = snprintf(out, size, "%lu|%lu|%lu|%s|%s", (unsigned long)e.seq, (unsigned long)e.ts,
                     (unsigned long)e.uptimeS, ev, de);
    return n > 0 && (size_t)n < size;
  }

  // Décodage d'un slot persisté (tolérant : chaîne vide ou malformée = slot vide).
  bool decodeSlot(uint8_t index, const char* in) {
    if (index >= Capacity) return false;
    Event& e = _slots[index];
    memset(&e, 0, sizeof(e));
    if (!in || in[0] == '\0') return true;
    unsigned long seq = 0, ts = 0, up = 0;
    const char* p = in;
    char* end = nullptr;
    seq = strtoul(p, &end, 10); if (!end || *end != '|') return false; p = end + 1;
    ts = strtoul(p, &end, 10);  if (!end || *end != '|') return false; p = end + 1;
    up = strtoul(p, &end, 10);  if (!end || *end != '|') return false; p = end + 1;
    const char* bar = strchr(p, '|');
    if (!bar) return false;
    size_t evLen = (size_t)(bar - p);
    if (evLen >= kEventChars) evLen = kEventChars - 1;
    memcpy(e.event, p, evLen); e.event[evLen] = '\0';
    copy(e.detail, sizeof(e.detail), bar + 1);
    e.seq = (uint32_t)seq; e.ts = (uint32_t)ts; e.uptimeS = (uint32_t)up;
    if (e.seq >= _nextSeq) _nextSeq = e.seq + 1;
    return true;
  }

  // Après décodage de tous les slots : repositionne la tête sur le slot le plus ancien (ou le
  // premier slot vide), pour que push() écrase bien le plus ancien.
  void recomputeHead() {
    uint8_t oldest = 0;
    uint32_t oldestSeq = 0xFFFFFFFFUL;
    bool anyEmpty = false;
    for (uint8_t i = 0; i < Capacity; ++i) {
      if (_slots[i].seq == 0) { if (!anyEmpty) { anyEmpty = true; oldest = i; } continue; }
      if (!anyEmpty && _slots[i].seq < oldestSeq) { oldestSeq = _slots[i].seq; oldest = i; }
    }
    _head = oldest;
  }

  uint8_t head() const { return _head; }
  uint32_t nextSeq() const { return _nextSeq; }
  const Event& slot(uint8_t i) const { return _slots[i % Capacity]; }

 private:
  static void copy(char* dst, size_t size, const char* src) {
    if (!size) return;
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
  }
  static void sanitize(char* dst, size_t size, const char* src) {
    copy(dst, size, src);
    for (char* c = dst; *c; ++c) if (*c == '|' || *c == '\n' || *c == '\r') *c = '/';
  }
  static void escape(char* dst, size_t size, const char* src) {
    size_t o = 0;
    for (const char* c = src; *c && o + 2 < size; ++c) {
      if (*c == '"' || *c == '\\') { dst[o++] = '\\'; dst[o++] = *c; }
      else if ((unsigned char)*c < 0x20) { dst[o++] = ' '; }
      // Le JSON voyage dans un corps x-www-form-urlencoded (champ log= du heartbeat, events= de
      // /ota-report) : '&' séparerait les champs et '+' serait décodé en espace par PHP.
      else if (*c == '&' || *c == '+' || *c == '%' || *c == '#') { dst[o++] = '/'; }
      else dst[o++] = *c;
    }
    dst[o] = '\0';
  }

  Event _slots[Capacity];
  uint8_t _head;
  uint32_t _nextSeq;
};

}  // namespace n3evt

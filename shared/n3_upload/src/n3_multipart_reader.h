#pragma once
// =============================================================================
// n3_upload — Lecteur multipart streamé head + corps + tail (pur)
// =============================================================================
// Transposition fidèle de la logique readBytes de camera_upload.cpp
// (MultipartCameraStream/MultipartFileStream) hors d'Arduino Stream : produit
// séquentiellement `head` (texte multipart), puis le corps via N3UploadSource
// (par chunks, jamais chargé entier), puis `tail` — avec les mêmes bornes de
// segments. totalSize() sert de Content-Length au POST streamé.
//
// PUR : testable nativement. L'adaptateur Arduino `Stream` (readBytes/available)
// qui enveloppe ce lecteur arrive avec le POST réseau (T3b).
// =============================================================================

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "n3_upload_source.h"

class N3MultipartReader {
 public:
  /** head/tail : chaînes NUL-terminées, possédées par l'appelant (non copiées). */
  N3MultipartReader(const char* head, N3UploadSource& body, const char* tail)
      : _head(head ? head : ""),
        _headLen(strlen(head ? head : "")),
        _body(body),
        _tail(tail ? tail : ""),
        _tailLen(strlen(tail ? tail : "")),
        _position(0) {}

  size_t totalSize() const { return _headLen + _body.size() + _tailLen; }

  /** Repositionne au début (retry d'envoi) — rembobine aussi la source. */
  void rewind() {
    _position = 0;
    _body.rewind();
  }

  /**
   * Copie jusqu'à length octets de la séquence head|corps|tail dans buffer.
   * Parité avec MultipartFileStream::readBytes : si la source rend 0 en plein
   * corps (fichier tronqué), on s'arrête (retour court) — l'appelant détecte
   * l'incohérence via totalSize().
   */
  size_t readBytes(char* buffer, size_t length) {
    if (!buffer || length == 0 || _position >= totalSize()) return 0;
    const size_t remain = totalSize() - _position;
    if (length > remain) length = remain;
    const size_t bodyLen = _body.size();
    size_t copied = 0;
    while (copied < length) {
      const size_t pos = _position;
      if (pos < _headLen) {
        size_t n = length - copied;
        if (n > _headLen - pos) n = _headLen - pos;
        memcpy(buffer + copied, _head + pos, n);
        copied += n;
        _position += n;
      } else if (pos < _headLen + bodyLen) {
        const size_t bodyRemaining = _headLen + bodyLen - pos;
        size_t want = length - copied;
        if (want > bodyRemaining) want = bodyRemaining;
        const size_t n = _body.readChunk(reinterpret_cast<uint8_t*>(buffer + copied), want);
        if (n == 0) break;  // source tarie prématurément : retour court
        copied += n;
        _position += n;
      } else {
        const size_t tailPos = pos - _headLen - bodyLen;
        size_t n = length - copied;
        if (n > _tailLen - tailPos) n = _tailLen - tailPos;
        memcpy(buffer + copied, _tail + tailPos, n);
        copied += n;
        _position += n;
      }
    }
    return copied;
  }

  size_t position() const { return _position; }

 private:
  const char* _head;
  size_t _headLen;
  N3UploadSource& _body;
  const char* _tail;
  size_t _tailLen;
  size_t _position;
};

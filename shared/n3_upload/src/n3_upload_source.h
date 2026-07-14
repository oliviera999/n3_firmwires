#pragma once
// =============================================================================
// n3_upload — Source de corps abstraite (pur)
// =============================================================================
// Généralisation de l'upload multipart d'uploadphotosserver (camera_upload.cpp)
// hors de toute dépendance caméra/SD : le corps d'un POST volumineux (JPEG,
// log…) est fourni via cette interface — taille connue d'avance (Content-Length
// d'un POST streamé) + lecture par chunks, SANS jamais allouer le corps complet.
//
// PUR : <stddef.h>/<stdint.h> uniquement — testable nativement. L'adaptateur
// Arduino (`File`/`Stream` → N3UploadSource) arrive avec le POST réseau (T3b),
// côté lib, pour être compilé/exercé par le build du firmware consommateur.
// =============================================================================

#include <stddef.h>
#include <stdint.h>

/** Source du corps binaire : taille connue, lecture séquentielle par chunks. */
class N3UploadSource {
 public:
  virtual ~N3UploadSource() {}
  /** Longueur totale du corps (connue d'avance, requise pour Content-Length). */
  virtual size_t size() const = 0;
  /** Repositionne au début (appelé avant chaque tentative d'envoi/retry). */
  virtual void rewind() = 0;
  /**
   * Copie jusqu'à maxLen octets dans dst à partir de la position courante.
   * Retourne le nombre d'octets copiés (0 = fin de source ou erreur — une
   * source fichier peut rendre moins que demandé sans être en erreur).
   */
  virtual size_t readChunk(uint8_t* dst, size_t maxLen) = 0;
};

/** Source mémoire (ex-MultipartCameraStream : framebuffer JPEG déjà en RAM). */
class N3UploadBufferSource : public N3UploadSource {
 public:
  N3UploadBufferSource(const uint8_t* data, size_t len)
      : _data(data), _len(data ? len : 0), _pos(0) {}

  size_t size() const override { return _len; }
  void rewind() override { _pos = 0; }

  size_t readChunk(uint8_t* dst, size_t maxLen) override {
    if (!dst || maxLen == 0 || _pos >= _len) return 0;
    size_t n = _len - _pos;
    if (n > maxLen) n = maxLen;
    for (size_t i = 0; i < n; ++i) dst[i] = _data[_pos + i];
    _pos += n;
    return n;
  }

 private:
  const uint8_t* _data;
  size_t _len;
  size_t _pos;
};

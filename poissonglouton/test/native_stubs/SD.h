#pragma once

// -----------------------------------------------------------------------------
// Mock SD/File en memoire pour tests natifs de PglEventJournal.
// Simule un systeme de fichiers minimal (un fichier = un std::vector<uint8_t>)
// supportant les operations utilisees par pgl_event_journal.cpp :
//   SD.open(path, mode), SD.exists, SD.remove, SD.rename
//   File : size(), seek(), read(), write(), close(), flush(), operator bool
// Modes : FILE_READ (lecture), FILE_APPEND (ajout en fin), "r+" (lecture+ecriture
// en place, sans troncature) — exactement ce dont le journal a besoin.
//
// SDMock::reset() vide le FS. SDMock::mounted controle si SD est "montee".
// -----------------------------------------------------------------------------

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace SDMock {

inline std::map<std::string, std::vector<uint8_t>>& fs() {
  static std::map<std::string, std::vector<uint8_t>> m;
  return m;
}
inline bool g_mounted = true;

inline void reset() {
  fs().clear();
  g_mounted = true;
}

}  // namespace SDMock

class File {
 public:
  File() : _valid(false), _data(nullptr), _pos(0), _append(false), _write(false) {}

  File(const std::string& path, const char* mode) {
    auto& m = SDMock::fs();
    _path = path;
    _append = (mode && mode[0] == 'a');
    _write = (mode && (mode[0] == 'a' || mode[0] == 'w' ||
                       (mode[0] == 'r' && mode[1] == '+')));

    if (mode && mode[0] == 'r' && mode[1] != '+') {
      // lecture seule : echoue si le fichier n'existe pas
      auto it = m.find(path);
      if (it == m.end()) { _valid = false; return; }
      _data = &it->second;
    } else if (mode && mode[0] == 'r' && mode[1] == '+') {
      // r+ : lecture+ecriture, echoue si absent (comme POSIX)
      auto it = m.find(path);
      if (it == m.end()) { _valid = false; return; }
      _data = &it->second;
    } else {
      // a / w : cree au besoin
      if (mode && mode[0] == 'w') m[path].clear();
      _data = &m[path];
    }

    _valid = true;
    _pos = _append ? _data->size() : 0;
  }

  explicit operator bool() const { return _valid; }

  size_t size() const { return _data ? _data->size() : 0; }

  bool seek(uint32_t pos) {
    if (!_valid || !_data) return false;
    if (pos > _data->size()) return false;
    _pos = pos;
    return true;
  }

  size_t read(uint8_t* buf, size_t len) {
    if (!_valid || !_data) return 0;
    size_t avail = _data->size() - _pos;
    size_t n = (len < avail) ? len : avail;
    memcpy(buf, _data->data() + _pos, n);
    _pos += n;
    return n;
  }

  size_t write(const uint8_t* buf, size_t len) {
    if (!_valid || !_data || !_write) return 0;
    if (_append) _pos = _data->size();
    // ecriture en place (et extension si on depasse la fin)
    if (_pos + len > _data->size()) _data->resize(_pos + len);
    memcpy(_data->data() + _pos, buf, len);
    _pos += len;
    return len;
  }

  void flush() {}
  void close() { _valid = false; _data = nullptr; }

 private:
  std::string _path;
  bool _valid;
  std::vector<uint8_t>* _data;
  size_t _pos;
  bool _append;
  bool _write;
};

class SDClass {
 public:
  File open(const char* path, const char* mode = FILE_READ) {
    if (!SDMock::g_mounted) return File();
    return File(std::string(path), mode);
  }
  bool exists(const char* path) {
    return SDMock::fs().count(std::string(path)) > 0;
  }
  bool remove(const char* path) {
    return SDMock::fs().erase(std::string(path)) > 0;
  }
  bool rename(const char* from, const char* to) {
    auto& m = SDMock::fs();
    auto it = m.find(std::string(from));
    if (it == m.end()) return false;
    m[std::string(to)] = it->second;
    m.erase(it);
    return true;
  }
};

inline SDClass SD;

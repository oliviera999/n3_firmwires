// Corps POST full-update canonique (aligné Ffp3HmacPostBody.php)

#include "ffp3_post_body.h"
#include <cstdio>
#include <cstring>

namespace Ffp3PostBody {

namespace {

bool strEq(const char* a, const char* b) {
  return strcmp(a, b) == 0;
}

void copyField(char* dest, size_t destSize, const char* value) {
  if (!dest || destSize == 0) return;
  snprintf(dest, destSize, "%s", value ? value : "");
}

bool setKnownField(FullUpdateValues& v, const char* key, const char* val) {
  if (strEq(key, "api_key")) { copyField(v.apiKey, sizeof(v.apiKey), val); return true; }
  if (strEq(key, "sensor")) { copyField(v.sensor, sizeof(v.sensor), val); return true; }
  if (strEq(key, "version")) { copyField(v.version, sizeof(v.version), val); return true; }
  if (strEq(key, "TempAir")) { copyField(v.tempAir, sizeof(v.tempAir), val); return true; }
  if (strEq(key, "Humidite")) { copyField(v.humidite, sizeof(v.humidite), val); return true; }
  if (strEq(key, "Pression")) { copyField(v.pression, sizeof(v.pression), val); return true; }
  if (strEq(key, "TempEau")) { copyField(v.tempEau, sizeof(v.tempEau), val); return true; }
  if (strEq(key, "EauPotager")) { copyField(v.eauPotager, sizeof(v.eauPotager), val); return true; }
  if (strEq(key, "EauAquarium")) { copyField(v.eauAquarium, sizeof(v.eauAquarium), val); return true; }
  if (strEq(key, "EauReserve")) { copyField(v.eauReserve, sizeof(v.eauReserve), val); return true; }
  if (strEq(key, "diffMaree")) { copyField(v.diffMaree, sizeof(v.diffMaree), val); return true; }
  if (strEq(key, "Luminosite")) { copyField(v.luminosite, sizeof(v.luminosite), val); return true; }
  if (strEq(key, "etatPompeAqua")) { copyField(v.etatPompeAqua, sizeof(v.etatPompeAqua), val); return true; }
  if (strEq(key, "etatPompeTank")) { copyField(v.etatPompeTank, sizeof(v.etatPompeTank), val); return true; }
  if (strEq(key, "etatHeat")) { copyField(v.etatHeat, sizeof(v.etatHeat), val); return true; }
  if (strEq(key, "etatUV")) { copyField(v.etatUV, sizeof(v.etatUV), val); return true; }
  if (strEq(key, "bouffeMatin")) { copyField(v.bouffeMatin, sizeof(v.bouffeMatin), val); return true; }
  if (strEq(key, "bouffeMidi")) { copyField(v.bouffeMidi, sizeof(v.bouffeMidi), val); return true; }
  if (strEq(key, "bouffeSoir")) { copyField(v.bouffeSoir, sizeof(v.bouffeSoir), val); return true; }
  if (strEq(key, "tempsGros")) { copyField(v.tempsGros, sizeof(v.tempsGros), val); return true; }
  if (strEq(key, "tempsPetits")) { copyField(v.tempsPetits, sizeof(v.tempsPetits), val); return true; }
  if (strEq(key, "aqThreshold")) { copyField(v.aqThreshold, sizeof(v.aqThreshold), val); return true; }
  if (strEq(key, "tankThreshold")) { copyField(v.tankThreshold, sizeof(v.tankThreshold), val); return true; }
  if (strEq(key, "chauffageThreshold")) { copyField(v.chauffageThreshold, sizeof(v.chauffageThreshold), val); return true; }
  if (strEq(key, "tempsRemplissageSec")) { copyField(v.tempsRemplissageSec, sizeof(v.tempsRemplissageSec), val); return true; }
  if (strEq(key, "limFlood")) { copyField(v.limFlood, sizeof(v.limFlood), val); return true; }
  if (strEq(key, "WakeUp")) { copyField(v.wakeUp, sizeof(v.wakeUp), val); return true; }
  if (strEq(key, "FreqWakeUp")) { copyField(v.freqWakeUp, sizeof(v.freqWakeUp), val); return true; }
  if (strEq(key, "bouffePetits")) { copyField(v.bouffePetits, sizeof(v.bouffePetits), val); return true; }
  if (strEq(key, "bouffeGros")) { copyField(v.bouffeGros, sizeof(v.bouffeGros), val); return true; }
  if (strEq(key, "mail")) { copyField(v.mail, sizeof(v.mail), val); return true; }
  if (strEq(key, "mailNotif")) { copyField(v.mailNotif, sizeof(v.mailNotif), val); return true; }
  if (strEq(key, "resetMode")) { copyField(v.resetMode, sizeof(v.resetMode), val); return true; }
  if (strEq(key, "tideEvent")) { copyField(v.tideEvent, sizeof(v.tideEvent), val); return true; }
  if (strEq(key, "tideTrend")) { copyField(v.tideTrend, sizeof(v.tideTrend), val); return true; }
  if (strEq(key, "tideNoiseMm")) { copyField(v.tideNoiseMm, sizeof(v.tideNoiseMm), val); return true; }
  if (strEq(key, "tideWindowMs")) { copyField(v.tideWindowMs, sizeof(v.tideWindowMs), val); return true; }
  if (strEq(key, "tideExtremeMm")) { copyField(v.tideExtremeMm, sizeof(v.tideExtremeMm), val); return true; }
  if (strEq(key, "configSynced")) { copyField(v.configSynced, sizeof(v.configSynced), val); return true; }
  if (strEq(key, "post_id")) { copyField(v.postId, sizeof(v.postId), val); return true; }
  return false;
}

void upsertExtra(ExtraStore& store, const char* key, const char* val) {
  for (uint8_t i = 0; i < store.count; ++i) {
    if (strcmp(store.pairs[i].key, key) == 0) {
      copyField(store.pairs[i].value, sizeof(store.pairs[i].value), val);
      return;
    }
  }
  if (store.count >= sizeof(store.pairs) / sizeof(store.pairs[0])) return;
  copyField(store.pairs[store.count].key, sizeof(store.pairs[store.count].key), key);
  copyField(store.pairs[store.count].value, sizeof(store.pairs[store.count].value), val);
  store.count++;
}

void sortExtras(ExtraStore& store) {
  for (uint8_t i = 0; i + 1 < store.count; ++i) {
    for (uint8_t j = i + 1; j < store.count; ++j) {
      if (strcmp(store.pairs[i].key, store.pairs[j].key) > 0) {
        ExtraPair tmp = store.pairs[i];
        store.pairs[i] = store.pairs[j];
        store.pairs[j] = tmp;
      }
    }
  }
}

int appendPair(char* out, size_t cap, int pos, const char* key, const char* val) {
  if (!key || !val || key[0] == '\0') return pos;
  int n = snprintf(out + pos, (pos < (int)cap) ? cap - (size_t)pos : 0,
                   (pos > 0) ? "&%s=%s" : "%s=%s", key, val);
  if (n < 0) return -1;
  pos += n;
  if ((size_t)pos >= cap) return -1;
  return pos;
}

}  // namespace

void applyExtraPairs(FullUpdateValues& values, const char* extraPairs) {
  if (!extraPairs || extraPairs[0] == '\0') return;

  char buf[512];
  size_t len = strlen(extraPairs);
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, extraPairs, len);
  buf[len] = '\0';

  char* save = nullptr;
  char* segment = strtok_r(buf, "&", &save);
  while (segment) {
    char* eq = strchr(segment, '=');
    if (eq) {
      *eq = '\0';
      const char* key = segment;
      const char* val = eq + 1;
      if (key[0] != '\0') {
        if (!setKnownField(values, key, val)) {
          upsertExtra(values.extras, key, val);
        }
      }
    }
    segment = strtok_r(nullptr, "&", &save);
  }
  sortExtras(values.extras);
}

int buildFullUpdateBody(char* out, size_t outSize, const FullUpdateValues& v) {
  if (!out || outSize == 0) return -1;
  int pos = 0;

  static const char* kMainKeys[] = {
      "api_key", "sensor", "version", "TempAir", "Humidite", "Pression", "TempEau",
      "EauPotager", "EauAquarium", "EauReserve", "diffMaree", "Luminosite",
      "etatPompeAqua", "etatPompeTank", "etatHeat", "etatUV",
      "bouffeMatin", "bouffeMidi", "bouffeSoir", "tempsGros", "tempsPetits",
      "aqThreshold", "tankThreshold", "chauffageThreshold",
      "tempsRemplissageSec", "limFlood", "WakeUp", "FreqWakeUp",
      "bouffePetits", "bouffeGros", "mail", "mailNotif",
  };

  auto emit = [&](const char* key, const char* val) -> bool {
    int next = appendPair(out, outSize, pos, key, val);
    if (next < 0) return false;
    pos = next;
    return true;
  };

  for (const char* key : kMainKeys) {
    const char* val = nullptr;
    if (strEq(key, "api_key")) val = v.apiKey;
    else if (strEq(key, "sensor")) val = v.sensor;
    else if (strEq(key, "version")) val = v.version;
    else if (strEq(key, "TempAir")) val = v.tempAir;
    else if (strEq(key, "Humidite")) val = v.humidite;
    else if (strEq(key, "Pression")) val = v.pression;
    else if (strEq(key, "TempEau")) val = v.tempEau;
    else if (strEq(key, "EauPotager")) val = v.eauPotager;
    else if (strEq(key, "EauAquarium")) val = v.eauAquarium;
    else if (strEq(key, "EauReserve")) val = v.eauReserve;
    else if (strEq(key, "diffMaree")) val = v.diffMaree;
    else if (strEq(key, "Luminosite")) val = v.luminosite;
    else if (strEq(key, "etatPompeAqua")) val = v.etatPompeAqua;
    else if (strEq(key, "etatPompeTank")) val = v.etatPompeTank;
    else if (strEq(key, "etatHeat")) val = v.etatHeat;
    else if (strEq(key, "etatUV")) val = v.etatUV;
    else if (strEq(key, "bouffeMatin")) val = v.bouffeMatin;
    else if (strEq(key, "bouffeMidi")) val = v.bouffeMidi;
    else if (strEq(key, "bouffeSoir")) val = v.bouffeSoir;
    else if (strEq(key, "tempsGros")) val = v.tempsGros;
    else if (strEq(key, "tempsPetits")) val = v.tempsPetits;
    else if (strEq(key, "aqThreshold")) val = v.aqThreshold;
    else if (strEq(key, "tankThreshold")) val = v.tankThreshold;
    else if (strEq(key, "chauffageThreshold")) val = v.chauffageThreshold;
    else if (strEq(key, "tempsRemplissageSec")) val = v.tempsRemplissageSec;
    else if (strEq(key, "limFlood")) val = v.limFlood;
    else if (strEq(key, "WakeUp")) val = v.wakeUp;
    else if (strEq(key, "FreqWakeUp")) val = v.freqWakeUp;
    else if (strEq(key, "bouffePetits")) val = v.bouffePetits;
    else if (strEq(key, "bouffeGros")) val = v.bouffeGros;
    else if (strEq(key, "mail")) val = v.mail;
    else if (strEq(key, "mailNotif")) val = v.mailNotif;
    if (!val || val[0] == '\0') continue;
    if (!emit(key, val)) return -1;
  }

  for (uint8_t i = 0; i < v.extras.count; ++i) {
    if (!emit(v.extras.pairs[i].key, v.extras.pairs[i].value)) return -1;
  }

  static const char* kSuffixKeys[] = {
      "resetMode", "tideEvent", "tideTrend", "tideNoiseMm", "tideWindowMs", "tideExtremeMm",
      "configSynced", "post_id",
  };
  for (const char* key : kSuffixKeys) {
    const char* val = nullptr;
    if (strEq(key, "resetMode")) val = v.resetMode;
    else if (strEq(key, "tideEvent")) val = v.tideEvent;
    else if (strEq(key, "tideTrend")) val = v.tideTrend;
    else if (strEq(key, "tideNoiseMm")) val = v.tideNoiseMm;
    else if (strEq(key, "tideWindowMs")) val = v.tideWindowMs;
    else if (strEq(key, "tideExtremeMm")) val = v.tideExtremeMm;
    else if (strEq(key, "configSynced")) val = v.configSynced;
    else if (strEq(key, "post_id")) val = v.postId;
    if (!val || val[0] == '\0') continue;
    if (!emit(key, val)) return -1;
  }

  if (pos >= (int)outSize) return -1;
  out[pos] = '\0';
  return pos;
}

}  // namespace Ffp3PostBody

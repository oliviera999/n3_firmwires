#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Corps POST full-update aligné sur serveur Ffp3HmacPostBody (ordre + format, sans clés dupliquées).
 * v13.90 : évite 401 HMAC quand mod_php reconstruit le corps depuis $_POST.
 */
namespace Ffp3PostBody {

struct ExtraPair {
  // key[24] depuis v15.25 (audit 2026-07, F2a). En [16] — soit 15 caracteres utiles —
  // copyField() tronquait SILENCIEUSEMENT 4 des 6 cles qui transitent reellement par
  // ExtraStore (les angles servo, seules a ne pas etre des champs connus de
  // setKnownField) : angleDistribGros, angleReposPetits, angleDistribPetits (18 car.,
  // la plus longue) et angleInterPetits. Le firmware emettait donc des noms de cles
  // FAUX. Sans consequence a ce jour — le serveur ne lit ces cles nulle part dans le
  // POST (SensorData n'a aucun champ d'angle ; les GPIO 118-123 lui appartiennent et
  // partent par GET) — et sans 401 non plus, puisque les deux cotes voient la meme
  // chaine tronquee. Mais toute evolution qui ferait lire ces champs au serveur
  // echouerait en silence. Cout : +64 octets sur FullUpdateValues (~921 -> ~985 o).
  char key[24];
  char value[32];
};

struct ExtraStore {
  ExtraPair pairs[8];
  uint8_t count{0};
};

/** Valeurs déjà formatées (chaînes) prêtes pour sérialisation canonique. */
struct FullUpdateValues {
  char apiKey[40];
  char sensor[24];
  char version[12];
  char tempAir[16];
  char humidite[16];
  char pression[16];
  char tempEau[16];
  char eauPotager[12];
  char eauAquarium[12];
  char eauReserve[12];
  char diffMaree[12];
  char luminosite[12];
  char etatPompeAqua[4];
  char etatPompeTank[4];
  char etatHeat[4];
  char etatUV[4];
  char bouffeMatin[8];
  char bouffeMidi[8];
  char bouffeSoir[8];
  char tempsGros[8];
  char tempsPetits[8];
  char aqThreshold[8];
  char tankThreshold[8];
  char chauffageThreshold[16];
  char tempsRemplissageSec[8];
  char limFlood[8];
  char wakeUp[4];
  char freqWakeUp[12];
  char bouffePetits[8];
  char bouffeGros[8];
  char mail[72];
  char mailNotif[12];
  char resetMode[4];
  char tideEvent[16];
  char tideTrend[8];
  char tideNoiseMm[8];
  char tideWindowMs[16];
  char tideExtremeMm[12];
  char configSynced[4];
  char postId[48];
  ExtraStore extras;
};

/** Parse extraPairs (key=value&...) : écrase les champs connus, stocke le reste pour tri alphabétique. */
void applyExtraPairs(FullUpdateValues& values, const char* extraPairs);

/** Sérialise le corps (sans & final) ; retourne longueur ou -1 si buffer trop petit. */
int buildFullUpdateBody(char* out, size_t outSize, const FullUpdateValues& values);

}  // namespace Ffp3PostBody

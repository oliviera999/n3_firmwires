#pragma once
// =============================================================================
// n3_common — Anti-brute-force du login web local (throttle / lockout, pur)
// =============================================================================
// Mutualisé depuis ffp5cs/include/login_throttle.h. Un handler POST /api/login
// qui compare user/pass sans limitation de débit permet un brute-force sur le LAN.
//
// PETITE machine d'état FAIL-SAFE, sans allocation dynamique, sans String, sans
// dépendance Arduino / config.h -> testable nativement (g++). Aucun effet de
// bord : la classe NE FAIT QUE compter et décider ; c'est l'appelant qui répond
// 429 et fournit millis().
//
// Le TEMPS est injecté (`nowMs`, typiquement millis()) pour rester testable sans
// matériel. Les paramètres de politique sont des constexpr compile-time.
//
// Politique (fail-safe : un utilisateur qui se trompe une fois n'est PAS bloqué ;
// seuls des échecs RAPPROCHÉS et RÉPÉTÉS déclenchent un lockout court) :
//   - kMaxFailures : nb d'échecs (dans la fenêtre) avant verrouillage ;
//   - kWindowMs    : fenêtre glissante — deux échecs séparés de plus de kWindowMs
//                    « cassent » la série (le compteur repart à 1) ;
//   - kLockoutMs   : durée du verrouillage une fois le seuil atteint.
//
// Sliding window : on ne mémorise que le timestamp du DERNIER échec et un
// compteur (pas de tableau de timestamps = 0 heap).
// =============================================================================

#include <cstdint>

namespace LoginThrottle {

// Politique par défaut (compile-time). Valeurs prudentes : 5 essais, fenêtre
// courte (30 s), verrou bref (60 s) pour ralentir un brute-force sans transformer
// une faute de frappe en DoS.
struct DefaultPolicy {
  static constexpr uint8_t  kMaxFailures = 5;            // échecs avant lockout
  static constexpr uint32_t kWindowMs    = 30000UL;      // fenêtre glissante : 30 s
  static constexpr uint32_t kLockoutMs   = 60000UL;      // verrou : 60 s
};

// Machine d'état paramétrée par une politique (struct de constexpr). Sans état
// caché : tous les membres sont explicites. Copiable trivialement, 0 allocation.
//
// NOTE temps : `nowMs` est un compteur monotone (millis()) qui boucle ~tous les
// 49,7 jours. Les comparaisons utilisent des différences non signées (now - ref),
// correctes au wrap tant que les durées comparées sont << 2^32 ms.
template <typename Policy = DefaultPolicy>
class Throttle {
 public:
  // À appeler sur ÉCHEC d'authentification.
  void registerFailure(uint32_t nowMs) {
    // Série expirée (premier échec, ou trop éloigné du précédent) -> redémarre.
    if (_failureCount == 0 || (uint32_t)(nowMs - _lastFailureMs) > Policy::kWindowMs) {
      _failureCount = 1;
    } else if (_failureCount < 0xFF) {
      _failureCount = (uint8_t)(_failureCount + 1);
    }
    _lastFailureMs = nowMs;

    if (_failureCount >= Policy::kMaxFailures) {
      _lockoutUntilMs = nowMs + Policy::kLockoutMs;
      _locked = true;
    }
  }

  // À appeler sur SUCCÈS d'authentification : remet tout à zéro.
  void registerSuccess() {
    _failureCount = 0;
    _lastFailureMs = 0;
    _lockoutUntilMs = 0;
    _locked = false;
  }

  // true tant que le verrou est actif. `nowMs` permet de détecter l'expiration
  // (const : ne mute pas l'état ; le nettoyage se fait au prochain
  // registerFailure via la fenêtre glissante).
  bool isLockedOut(uint32_t nowMs) const {
    if (!_locked) return false;
    // Soustraction signée pour gérer le wrap millis().
    if ((int32_t)(nowMs - _lockoutUntilMs) < 0) {
      return true;
    }
    return false;  // expiré -> plus verrouillé
  }

  // Millisecondes restantes avant déverrouillage (0 si non verrouillé / expiré).
  uint32_t remainingLockoutMs(uint32_t nowMs) const {
    if (!isLockedOut(nowMs)) return 0;
    return (uint32_t)(_lockoutUntilMs - nowMs);
  }

  // Accès lecture seule (diagnostic / tests).
  uint8_t failureCount() const { return _failureCount; }

 private:
  uint8_t  _failureCount  = 0;  // échecs consécutifs dans la fenêtre courante
  uint32_t _lastFailureMs = 0;  // timestamp du dernier échec (fenêtre glissante)
  uint32_t _lockoutUntilMs = 0; // instant de fin de verrou (millis())
  bool     _locked        = false;  // verrou armé (jusqu'à _lockoutUntilMs)
};

}  // namespace LoginThrottle

#pragma once
// =============================================================================
// FFP5CS — Anti-brute-force du login web local (throttle / lockout, logique pure)
// =============================================================================
// Le handler POST /api/login (web_server.cpp) compare user/pass en clair puis
// génère un token de session (HW RNG). Sans limitation de débit, un attaquant sur
// le LAN peut tester les identifiants en boucle (brute-force / dictionnaire).
//
// Cette unité est une PETITE machine d'état FAIL-SAFE, sans allocation dynamique,
// sans String, sans dépendance Arduino / config.h -> testable nativement (g++) au
// même titre que les modules purs du chantier C4 (cf. ReservoirLowSecurity::State,
// HeaterControl::evaluate). Aucun effet de bord : la classe NE FAIT QUE compter et
// décider ; c'est l'appelant (web_server.cpp) qui répond 429 et appelle millis().
//
// Le TEMPS est injecté (`nowMs`, typiquement millis()) pour rester testable sans
// matériel. Les paramètres de politique sont des constexpr compile-time.
//
// Politique (fail-safe -> un utilisateur légitime qui se trompe une fois n'est PAS
// bloqué ; seuls des échecs RAPPROCHÉS et RÉPÉTÉS déclenchent un lockout court) :
//   - kMaxFailures      : nb d'échecs (dans la fenêtre) avant verrouillage ;
//   - kWindowMs         : fenêtre glissante — deux échecs séparés de plus de
//                         kWindowMs « cassent » la série (le compteur repart à 1) ;
//   - kLockoutMs        : durée du verrouillage une fois le seuil atteint.
//
// Sliding window : on ne mémorise que le timestamp du DERNIER échec et un compteur.
// Si l'échec courant survient plus de kWindowMs après le précédent, la série est
// considérée expirée et le compteur redémarre à 1 (pas d'accumulation d'échecs
// isolés dans le temps -> fail-safe). Pas de tableau de timestamps = 0 heap.
// =============================================================================

#include <cstdint>

namespace LoginThrottle {

// -----------------------------------------------------------------------------
// Politique par défaut (compile-time). Valeurs prudentes : laisse de la marge à
// l'utilisateur légitime (5 essais), fenêtre courte (30 s) et verrou bref (60 s)
// pour ralentir un brute-force sans transformer une faute de frappe en DoS.
// -----------------------------------------------------------------------------
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
// correctes au wrap tant que les durées comparées sont << 2^32 ms (cas ici :
// quelques dizaines de secondes), à l'image des autres modules du dépôt.
template <typename Policy = DefaultPolicy>
class Throttle {
 public:
  // À appeler sur ÉCHEC d'authentification. Met à jour la série d'échecs et,
  // si le seuil est atteint dans la fenêtre, arme le verrou jusqu'à
  // nowMs + kLockoutMs.
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

  // À appeler sur SUCCÈS d'authentification : remet tout à zéro (l'utilisateur
  // légitime repart d'une ardoise propre, y compris après quelques erreurs).
  void registerSuccess() {
    _failureCount = 0;
    _lastFailureMs = 0;
    _lockoutUntilMs = 0;
    _locked = false;
  }

  // true tant que le verrou est actif. `nowMs` permet de détecter l'expiration
  // (const : ne mute pas l'état ; le nettoyage du compteur se fait au prochain
  // registerFailure via la fenêtre glissante). Une fois le verrou expiré, l'accès
  // est de nouveau autorisé.
  bool isLockedOut(uint32_t nowMs) const {
    if (!_locked) return false;
    // Verrou encore actif tant que (nowMs - lockoutUntil) n'a pas « dépassé » 0.
    // Soustraction signée pour gérer le wrap millis() comme isAuthenticated().
    if ((int32_t)(nowMs - _lockoutUntilMs) < 0) {
      return true;
    }
    return false;  // expiré -> plus verrouillé
  }

  // Millisecondes restantes avant déverrouillage (0 si non verrouillé / expiré).
  // Utile pour un header Retry-After ; purement informatif.
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

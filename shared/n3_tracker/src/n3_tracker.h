/**
 * n3_tracker — Logique pure du tracker solaire (2 axes, 2 LDR par axe).
 *
 * Extraction testable de la logique msp (audit tracker 2026-07) :
 *  - PeakTracker  : suivi du pic d'une LDR pendant un balayage (mode balayage).
 *  - fusePeaks    : fusion des pics des deux LDR d'un axe, ponderee par
 *                   l'amplitude, avec validation (une LDR ombragee/HS ne divise
 *                   plus l'angle par deux) et repli sur l'angle courant.
 *  - fineWindow   : fenetre du second passage (balayage grossier puis fin).
 *  - DiffAxis     : asservissement differentiel classique — equilibrer les deux
 *                   LDR d'un axe par petits pas, avec zone morte, arret sur
 *                   renversement (anti-oscillation), butee et divergence.
 *
 * Aucune dependance Arduino : le firmware fait les analogRead/servo.write et
 * appelle ces fonctions ; tout est verifiable en tests natifs Unity.
 */
#pragma once

namespace N3Tracker {

/** Borne un angle a [minAngle, maxAngle]. */
int clampAngle(int angle, int minAngle, int maxAngle);

// --- Calibration / egalisation des LDR ---

/** Gain neutre (aucune correction) en pour-mille. */
constexpr int kGainNeutralPermille = 1000;

/** Applique un gain en pour-mille a une lecture brute, borne a [0, maxValue]. */
int applyGainPermille(int raw, int gainPermille, int maxValue);

/**
 * Gains d'egalisation d'un groupe de LDR a partir de references mesurees sous
 * un meme eclairement (ex. pics d'un balayage complet en plein soleil, ou
 * lecture sous lumiere diffuse uniforme) :
 *   gain_i = 1000 * max(refs) / refs_i, borne [minGain, maxGain] pour-mille.
 * Le capteur le plus sensible sert d'etalon (gain 1000) ; les autres sont
 * amplifies pour rapporter pareil sous le meme eclairement. Une reference
 * < minRef (capteur ombrage/HS pendant la mesure) recoit le gain neutre.
 * Retourne true si TOUTES les references etaient exploitables (calibration
 * complete), false sinon.
 */
bool computeEqualizationGains(const int* refs, int count, int minRef,
                              int minGainPermille, int maxGainPermille,
                              int* gainsOut);

/**
 * Suivi du pic d'un capteur pendant un balayage. feed() est appele a chaque
 * position avec la lecture filtree ; a amplitude egale la premiere position
 * est conservee (comportement historique msp).
 */
struct PeakTracker {
  int bestPos = -1;
  int bestVal = -1;

  void reset();
  void feed(int pos, int value);
  /** Pic exploitable : au moins une lecture et amplitude >= minValue. */
  bool valid(int minValue) const;
};

/**
 * Fusion des pics des deux LDR d'un axe :
 *  - deux pics valides  -> moyenne des positions ponderee par l'amplitude ;
 *  - un seul pic valide -> sa position (l'autre LDR est ombragee/HS) ;
 *  - aucun pic valide   -> fallbackAngle (ne pas bouger).
 * Resultat borne a [minAngle, maxAngle].
 */
int fusePeaks(const PeakTracker& p1, const PeakTracker& p2,
              int minPeakValue, int fallbackAngle,
              int minAngle, int maxAngle);

/** Fenetre [from, to] du balayage fin autour d'un centre, bornee a l'axe. */
struct ScanWindow {
  int from;
  int to;
};
ScanWindow fineWindow(int center, int halfWidth, int minAngle, int maxAngle);

/** Configuration d'un axe pour l'asservissement differentiel. */
struct DiffAxisConfig {
  int minAngle;
  int maxAngle;
  int deadband;  /**< |lumA - lumB| <= deadband -> axe equilibre (raw ADC). */
  int stepDeg;   /**< Pas de correction en degres (> 0). */
  bool invert;   /**< Inverse le sens (lumA > lumB -> -angle au lieu de +angle). */
};

/** Decision d'un pas d'asservissement differentiel. */
struct DiffStep {
  int nextAngle;   /**< Angle a appliquer (== courant si aucun mouvement). */
  bool balanced;   /**< Ecart dans la zone morte : cible atteinte. */
  bool finished;   /**< Stop : balanced, renversement, butee ou divergence. */
  bool diverged;   /**< L'ecart augmente en avancant : sens probablement inverse
                        (voir DiffAxisConfig::invert) ou lumiere incoherente. */
};

/**
 * Controleur differentiel d'un axe. Boucle firmware type :
 *   ctl.reset();
 *   while (pas de budget epuise) {
 *     lire lumA, lumB (filtres) ;
 *     DiffStep s = ctl.step(angle, lumA, lumB);
 *     si s.nextAngle != angle -> servo.write(s.nextAngle), attendre ;
 *     si s.finished -> stop ;
 *   }
 * L'etat interne detecte le renversement de direction (croisement de
 * l'equilibre : on s'arrete au lieu d'osciller) et la divergence (l'ecart
 * grossit DIFF_DIVERGE_LIMIT pas de suite : sens de montage inverse ou
 * eclairage incoherent -> stop defensif).
 */
class DiffAxis {
 public:
  static const int DIVERGE_LIMIT = 3;

  explicit DiffAxis(const DiffAxisConfig& cfg) : cfg_(cfg) { reset(); }

  void reset();
  DiffStep step(int currentAngle, int lumA, int lumB);

 private:
  DiffAxisConfig cfg_;
  int lastDir_;
  int prevMagnitude_;
  int divergeCount_;
};

}  // namespace N3Tracker

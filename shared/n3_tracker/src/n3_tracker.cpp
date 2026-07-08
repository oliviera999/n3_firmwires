/** n3_tracker — implementation (logique pure, sans Arduino). */

#include "n3_tracker.h"

namespace N3Tracker {

int clampAngle(int angle, int minAngle, int maxAngle) {
  if (angle < minAngle) return minAngle;
  if (angle > maxAngle) return maxAngle;
  return angle;
}

void PeakTracker::reset() {
  bestPos = -1;
  bestVal = -1;
}

void PeakTracker::feed(int pos, int value) {
  // Strictement superieur : a amplitude egale on garde la premiere position
  // rencontree (comportement historique du balayage msp).
  if (value > bestVal) {
    bestVal = value;
    bestPos = pos;
  }
}

bool PeakTracker::valid(int minValue) const {
  return bestPos >= 0 && bestVal >= minValue;
}

int fusePeaks(const PeakTracker& p1, const PeakTracker& p2,
              int minPeakValue, int fallbackAngle,
              int minAngle, int maxAngle) {
  const bool v1 = p1.valid(minPeakValue);
  const bool v2 = p2.valid(minPeakValue);

  int fused;
  if (v1 && v2) {
    const long w1 = (p1.bestVal > 0) ? p1.bestVal : 0;
    const long w2 = (p2.bestVal > 0) ? p2.bestVal : 0;
    const long den = w1 + w2;
    if (den <= 0) {
      // Deux pics "valides" mais nuls (minPeakValue <= 0) : moyenne simple.
      fused = (p1.bestPos + p2.bestPos) / 2;
    } else {
      fused = (int)((p1.bestPos * w1 + p2.bestPos * w2) / den);
    }
  } else if (v1) {
    fused = p1.bestPos;
  } else if (v2) {
    fused = p2.bestPos;
  } else {
    fused = fallbackAngle;
  }
  return clampAngle(fused, minAngle, maxAngle);
}

ScanWindow fineWindow(int center, int halfWidth, int minAngle, int maxAngle) {
  if (halfWidth < 0) halfWidth = 0;
  ScanWindow w;
  w.from = clampAngle(center - halfWidth, minAngle, maxAngle);
  w.to = clampAngle(center + halfWidth, minAngle, maxAngle);
  return w;
}

void DiffAxis::reset() {
  lastDir_ = 0;
  prevMagnitude_ = -1;
  divergeCount_ = 0;
}

DiffStep DiffAxis::step(int currentAngle, int lumA, int lumB) {
  DiffStep r;
  r.nextAngle = currentAngle;
  r.balanced = false;
  r.finished = false;
  r.diverged = false;

  const int error = lumA - lumB;
  const int magnitude = (error < 0) ? -error : error;

  if (magnitude <= cfg_.deadband) {
    r.balanced = true;
    r.finished = true;
    return r;
  }

  int dir = (error > 0) ? 1 : -1;
  if (cfg_.invert) dir = -dir;

  if (lastDir_ != 0 && dir != lastDir_) {
    // Renversement : on vient de croiser l'equilibre -> stop, pas d'oscillation.
    r.finished = true;
    return r;
  }

  if (lastDir_ != 0 && dir == lastDir_) {
    // Meme direction : l'ecart doit diminuer. S'il grossit plusieurs pas de
    // suite, le sens est probablement inverse (cfg.invert a corriger) ou la
    // lumiere est incoherente -> stop defensif.
    if (prevMagnitude_ >= 0 && magnitude > prevMagnitude_) {
      if (++divergeCount_ >= DIVERGE_LIMIT) {
        r.finished = true;
        r.diverged = true;
        return r;
      }
    } else {
      divergeCount_ = 0;
    }
  }

  const int next = clampAngle(currentAngle + dir * cfg_.stepDeg,
                              cfg_.minAngle, cfg_.maxAngle);
  if (next == currentAngle) {
    // Butee atteinte sans equilibre.
    r.finished = true;
    return r;
  }

  lastDir_ = dir;
  prevMagnitude_ = magnitude;
  r.nextAngle = next;
  return r;
}

}  // namespace N3Tracker

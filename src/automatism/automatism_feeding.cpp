#include "automatism/automatism_feeding_v2.h"
#include "config.h"

AutomatismFeeding::AutomatismFeeding(SystemActuators& acts, ConfigManager& cfg)
    : _acts(acts), _cfg(cfg), _tempsGros(5), _tempsPetits(3) {}

void AutomatismFeeding::feedSmall() {
    uint16_t duration = getSmallDuration();
    _acts.feedSmallFish(duration);
}

void AutomatismFeeding::feedBig() {
    uint16_t duration = getBigDuration();
    _acts.feedBigFish(duration);
}

uint16_t AutomatismFeeding::getBigDuration() const {
    return _tempsGros;
}

uint16_t AutomatismFeeding::getSmallDuration() const {
    return _tempsPetits;
}


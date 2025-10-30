#pragma once

#include "automatism/automatism_state.h"

class AutomatismDisplayController {
public:
  explicit AutomatismDisplayController(Automatism& core);
  void updateDisplay(const AutomatismRuntimeContext& ctx);
  uint32_t getRecommendedDisplayIntervalMs(uint32_t nowMs) const;

private:
  Automatism& _core;
};



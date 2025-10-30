#pragma once

#include "automatism/automatism_state.h"

class AutomatismRefillController {
public:
  explicit AutomatismRefillController(Automatism& core);
  void process(const AutomatismRuntimeContext& ctx);

private:
  Automatism& _core;
};



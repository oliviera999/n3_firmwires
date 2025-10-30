#pragma once

#include "automatism/automatism_state.h"

class AutomatismAlertController {
public:
  explicit AutomatismAlertController(Automatism& core);
  void process(const AutomatismRuntimeContext& ctx);

private:
  Automatism& _core;
};



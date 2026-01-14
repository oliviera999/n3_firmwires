#pragma once

#include "app_context.h"

namespace BootstrapStorage {

void initialize(AppContext& ctx,
                unsigned long& lastDigestMs,
                uint32_t& lastDigestSeq);

}  // namespace BootstrapStorage



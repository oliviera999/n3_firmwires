#pragma once

#include <cstddef>

class PSRAMOptimizer {
public:
    static bool psramAvailable;
    static size_t psramFree;
    
    static void init() {}
    static bool isPSRAMAvailable() { return false; }
    static size_t getFreePSRAM() { return 0; }
    static size_t getTotalPSRAM() { return 0; }
};

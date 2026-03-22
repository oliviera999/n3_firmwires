/*
 * GCC 14 / libstdc++ (shared_ptr dans FS, SD_MMC) : symboles __atomic_* 4 octets
 * parfois non fournis par la toolchain Xtensa — stubs minimalistes pour le link.
 * Voir toolchain xtensa-esp-elf + Arduino-ESP32 3.3.x.
 */
#include <stdint.h>

__attribute__((weak)) int __atomic_fetch_add_4(volatile int *ptr, int val, int memorder) {
    (void)memorder;
    int old = *ptr;
    *ptr = old + val;
    return old;
}

__attribute__((weak)) int __atomic_fetch_sub_4(volatile int *ptr, int val, int memorder) {
    (void)memorder;
    int old = *ptr;
    *ptr = old - val;
    return old;
}

/*
 * GCC 14 / libstdc++ (shared_ptr dans Network, FS, SD) : __atomic_fetch_* 4 octets
 * parfois absents de la toolchain Xtensa — stubs pour le link.
 * Voir toolchain xtensa-esp-elf + Arduino-ESP32 3.3.x (idem uploadphotosserver).
 */
#include <stdint.h>

int __atomic_fetch_add_4(volatile int *ptr, int val, int memorder) {
    (void)memorder;
    int old = *ptr;
    *ptr = old + val;
    return old;
}

int __atomic_fetch_sub_4(volatile int *ptr, int val, int memorder) {
    (void)memorder;
    int old = *ptr;
    *ptr = old - val;
    return old;
}

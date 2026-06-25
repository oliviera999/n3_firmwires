#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

#include <stdio.h>

// Configuration Unity pour les tests natifs (hote) de poissonglouton.
#ifndef UNITY_OUTPUT_CHAR
#define UNITY_OUTPUT_CHAR(a) putchar(a)
#endif
#ifndef UNITY_OUTPUT_START
#define UNITY_OUTPUT_START()
#endif
#ifndef UNITY_OUTPUT_COMPLETE
#define UNITY_OUTPUT_COMPLETE()
#endif
#ifndef UNITY_OUTPUT_FLUSH
#define UNITY_OUTPUT_FLUSH() fflush(stdout)
#endif
#ifndef UNITY_PRINT_EOL
#define UNITY_PRINT_EOL() UNITY_OUTPUT_CHAR('\n')
#endif

#endif  // UNITY_CONFIG_H

#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

#include <stdio.h>

// Configuration Unity pour tests unitaires natifs
// Ce fichier est requis par Unity framework pour les tests

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

#ifndef UNITY_EXCLUDE_FLOAT
#define UNITY_EXCLUDE_FLOAT 0
#endif

#ifndef UNITY_EXCLUDE_DOUBLE
#define UNITY_EXCLUDE_DOUBLE 0
#endif

#ifndef UNITY_EXCLUDE_SETJMP_H
#define UNITY_EXCLUDE_SETJMP_H 0
#endif

#endif // UNITY_CONFIG_H

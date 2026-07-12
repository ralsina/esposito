#ifndef MXML_CONFIG_H
#define MXML_CONFIG_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#define MXML_VERSION "Mini-XML v4.0.4"

// No pthreads — apps are single-threaded, use the non-threaded fallback.
#undef HAVE_PTHREAD_H

#endif

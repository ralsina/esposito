/*
 * OS printf wrappers with floating point support
 */

#ifndef OS_PRINTF_H
#define OS_PRINTF_H

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int os_printf(const char *format, ...);
int os_sprintf(char *str, const char *format, ...);
int os_snprintf(char *str, size_t size, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif // OS_PRINTF_H
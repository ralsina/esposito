#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

bool config_bind_app(const char *app_name);
void config_unbind_app(void);

FILE *config_open_read(const char *key_path);
FILE *config_open_write(const char *key_path);

bool config_exists(const char *key_path);
bool config_delete(const char *key_path);

char *config_read_all_alloc(const char *key_path, size_t *out_len);
void config_free(void *ptr);

int config_get_int(const char *key_path, int default_value);
float config_get_float(const char *key_path, float default_value);
bool config_get_bool(const char *key_path, bool default_value);
size_t config_get_string(const char *key_path,
                         const char *default_value,
                         char *out,
                         size_t out_size);

bool config_set_int(const char *key_path, int value);
bool config_set_float(const char *key_path, float value);
bool config_set_bool(const char *key_path, bool value);
bool config_set_string(const char *key_path, const char *value);

#ifdef __cplusplus
}
#endif

#endif
#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include <stdbool.h>
#include <stddef.h>

// Checkpoint API for apps
void checkpoint_save_string(const char *key, const char *value);
const char *checkpoint_load_string(const char *key);
void checkpoint_save_int(const char *key, int value);
int checkpoint_load_int(const char *key);

// Checkpoint management
bool checkpoint_open(const char *app_name);
bool checkpoint_save(void);
void checkpoint_close(void);

#endif // CHECKPOINT_H

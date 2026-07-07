#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>
#include <stdbool.h>

bool battery_init(void);
bool battery_is_available(void);
int battery_read_millivolts(void);
void battery_deinit(void);

#endif

#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool wifi_init(void);
bool wifi_connect(const char *ssid, const char *password);
void wifi_disconnect(void);
bool wifi_is_connected(void);
bool wifi_scan(void);
int wifi_get_scan_count(void);
bool wifi_get_scan_result(int index, char *ssid, size_t ssid_size, int *rssi);

#ifdef __cplusplus
}
#endif

#endif

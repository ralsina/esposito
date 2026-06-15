#include "wifi.h"
#include <stdio.h>
#include <string.h>

bool wifi_init(void) { return false; }
bool wifi_connect(const char *ssid, const char *password) { (void)ssid; (void)password; return false; }
void wifi_disconnect(void) {}
bool wifi_is_connected(void) { return false; }
bool wifi_scan(void) { return false; }
int wifi_get_scan_count(void) { return 0; }
bool wifi_get_scan_result(int index, char *ssid, size_t ssize, int *rssi) {
    (void)index; (void)ssid; (void)ssize; (void)rssi;
    return false;
}

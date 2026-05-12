#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>
#include <stdint.h>

#define WIFI_MAX_SSID 32
#define WIFI_MAX_PASSWORD 64
#define WIFI_MAX_SCAN_RESULTS 20
#define WIFI_IP_STR_LEN 16

#ifdef __cplusplus
extern "C" {
#endif

// Initialize WiFi: read config from SD card, connect if configured
bool wifi_init(void);

// Connection status
bool wifi_is_connected(void);
const char *wifi_get_ip(void);

// Scan for networks (blocking). Returns number of networks found.
int wifi_scan(void);

// Get scan result details. Must call wifi_scan first.
const char *wifi_scan_get_ssid(int index);
int wifi_scan_get_rssi(int index);

// Connect to a network
bool wifi_connect(const char *ssid, const char *password);

// Disconnect
void wifi_disconnect(void);

// Save WiFi config to SD card
bool wifi_save_config(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif

#endif // WIFI_H

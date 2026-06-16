#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define WIFI_MAX_SSID 32
#define WIFI_MAX_PASSWORD 64
#define WIFI_MAX_SCAN_RESULTS 20
#define WIFI_IP_STR_LEN 16

#ifdef __cplusplus
extern "C" {
#endif

// Initialize WiFi: read credentials from NVS (migrating from SD card on first
// boot if legacy plaintext files are present), connect if configured.
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

// SNTP / time sync state for current boot
bool wifi_time_is_synchronized(void);
time_t wifi_time_last_sync(void);

// Save WiFi credentials to NVS (not exposed to apps; apps see wifi_scan_* only).
bool wifi_save_config(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif

#endif // WIFI_H

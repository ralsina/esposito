#ifndef BLE_KEYBOARD_H
#define BLE_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the BLE HID Host stack and start background scanning.
// This function blocks until BLE init completes (or fails).
// Returns true if BLE is available and was initialized.
bool ble_keyboard_init(void);

// Start BLE init in a background task (non-blocking).
// Use this during boot to avoid blocking if BLE init is slow.
void ble_keyboard_init_async(void);

// Whether BLE HID Host is available on this board.
bool ble_keyboard_is_available(void);

// Whether BLE init is currently in progress (background task).
bool ble_keyboard_is_initializing(void);

// --- Scanning ---

// Start a scan for BLE HID devices. Returns number of devices found so far.
// The scan runs for duration_seconds, then stops. Results accumulate.
int ble_keyboard_start_scan(int duration_seconds);

// Number of devices found in the current/latest scan.
int ble_keyboard_get_scan_count(void);

// Get scan result details by index.
const char *ble_keyboard_get_scan_name(int index);
int ble_keyboard_get_scan_rssi(int index);
const char *ble_keyboard_get_scan_addr(int index);

// --- Connection ---

// Connect to a device from the scan results by index.
bool ble_keyboard_connect(int scan_index);

// Disconnect the currently connected device.
void ble_keyboard_disconnect(void);

// Whether a BLE keyboard is connected.
bool ble_keyboard_is_connected(void);

// Name of the connected device (or empty string).
const char *ble_keyboard_get_connected_name(void);

// Save the currently connected device address to settings (for auto-reconnect).
void ble_keyboard_save_device(void);

// Attempt to reconnect to the last saved device. Called after init.
void ble_keyboard_reconnect(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_KEYBOARD_H

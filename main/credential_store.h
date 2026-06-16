#ifndef CREDENTIAL_STORE_H
#define CREDENTIAL_STORE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the credential store. Must be called once before any other
// credential_store_* function. Safe to call multiple times.
// Internally initializes NVS flash if needed.
bool credential_store_init(void);

// Read the stored WiFi SSID. Returns true if a non-empty SSID was found
// and written to out (always null-terminated). Returns false if no SSID
// is stored or on error.
bool credential_store_get_ssid(char *out, size_t out_len);

// Read the stored WiFi password. Returns true if a password was found
// and written to out (always null-terminated). Returns false if no
// password key exists in storage; callers should treat that as "no
// password required" (open network), not as a missing configuration.
// Use credential_store_get_ssid() to determine whether any credentials
// are configured at all.
bool credential_store_get_password(char *out, size_t out_len);

// Store WiFi credentials. Empty SSID is rejected. NULL or empty
// password is allowed (open network). Returns true on success.
bool credential_store_set(const char *ssid, const char *password);

// Erase any stored WiFi credentials.
void credential_store_erase(void);

#ifdef __cplusplus
}
#endif

#endif // CREDENTIAL_STORE_H

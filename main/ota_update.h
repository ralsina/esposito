#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdbool.h>
#include <stddef.h>

// GitHub releases API endpoints.
//   - /releases/latest returns the newest non-prerelease.
//   - /releases returns all releases (sorted desc, newest first) and is used
//     when the beta-channel toggle is on (os_settings key below).
#define OTA_RELEASES_API_STABLE "https://api.github.com/repos/ralsina/esposito/releases/latest"
#define OTA_RELEASES_API_BETA   "https://api.github.com/repos/ralsina/esposito/releases"

// OS settings key that selects the beta channel. Default is 0 (stable).
// Set to 1 to also consider pre-releases when checking for updates.
#define OTA_BETA_CHANNEL_KEY "ota/beta_channel"

#ifdef __cplusplus
extern "C" {
#endif

// Currently-running firmware version (from git describe, embedded at build).
const char *ota_firmware_version(void);

// Query GitHub releases for the latest version appropriate to the configured
// channel (stable unless ota/beta_channel = 1). Returns true and writes the
// tag (e.g. "v1.2.3") to latest_version when a strictly newer release than
// the running firmware is available; returns false otherwise. Also caches
// the firmware.bin and firmware.bin.sig asset URLs internally for the
// subsequent ota_apply_update() call.
//
// Rollback protection: a release whose semver is not strictly greater than
// the running firmware is refused over the network. (Downgrades are still
// possible via the unsigned SD-card install path -- physical access is root.)
bool ota_check_for_update(char *latest_version, size_t max_len);

// Download the cached release's firmware.bin and firmware.bin.sig, verify
// the ECDSA-P256 signature against the public key embedded in main/ota_keys.h,
// and only then hand off to the update stub to apply. Does not return on
// success (reboots into the stub).
const char *ota_apply_update(void);

// Boot-time SD-card recovery check: if /sdcard/system/firmware.bin exists,
// hand off to the update stub to apply it. UNSIGNED BY DESIGN -- this is the
// trusted physical-access path documented in docs/trust-model.md and is the
// user's downgrade/recovery escape hatch.
void ota_recovery_check(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdbool.h>
#include <stddef.h>

#define OTA_FIRMWARE_URL "https://esposito.ralsina.me/firmware.bin"
#define OTA_VERSION_URL  "https://esposito.ralsina.me/version.txt"

#ifdef __cplusplus
extern "C" {
#endif

const char *ota_firmware_version(void);

bool ota_check_for_update(char *latest_version, size_t max_len);

bool ota_download_to_sd(const char *url);

void ota_apply_update(void);

void ota_recovery_check(void);

#ifdef __cplusplus
}
#endif

#endif

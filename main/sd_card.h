#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize SD card and mount FAT filesystem
bool sd_card_init(void);

// Check if SD card is mounted
bool sd_card_is_mounted(void);

// Get mount point path
const char* sd_card_get_mount_point(void);

// List files in directory (path relative to mount point)
bool sd_card_list_files(const char *path);

// Read file contents into buffer
bool sd_card_read_file(const char *path, char *buffer, size_t max_len);

// Write data to file
bool sd_card_write_file(const char *path, const char *data);

// Unmount SD card
void sd_card_unmount(void);

#ifdef __cplusplus
}
#endif

#endif // SD_CARD_H
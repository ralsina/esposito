#include "sd_card.h"
#include "hardware_config.h"
#include "esp_log.h"
#include <string.h>
#include <dirent.h>

static const char *TAG = "sd_card_test";

#if BOARD_HAS_SD_CARD
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

static bool sd_card_mounted = false;

bool sd_card_init(void) {
    ESP_LOGI(TAG, "Using SDSPI mode (separate from display):");
    ESP_LOGI(TAG, "  MISO=%d, MOSI=%d, SCLK=%d, CS=%d", BOARD_SD_MISO_PIN, BOARD_SD_MOSI_PIN, BOARD_SD_CLK_PIN, BOARD_SD_CS_PIN);

    esp_err_t ret;

    // Try SDSPI mode (SPI mode - like Arduino SD library)
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;

    // Configure SDSPI host (separate bus from display)
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BOARD_SD_MOSI_PIN,
        .miso_io_num = BOARD_SD_MISO_PIN,
        .sclk_io_num = BOARD_SD_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4092,
    };

    ESP_LOGI(TAG, "Initializing SD SPI bus...");
    ret = spi_bus_initialize(BOARD_SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SD_SPI_HOST;
    host.max_freq_khz = 20000;

    ESP_LOGI(TAG, "Attempting SDSPI mount...");

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = BOARD_SD_CS_PIN;
    slot_config.host_id = BOARD_SD_SPI_HOST;

    ret = esp_vfs_fat_sdspi_mount(
        "/sdcard",
        &host,
        &slot_config,
        &mount_config,
        &card
    );

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted with SDSPI mode");
        sdmmc_card_print_info(stdout, card);
        sd_card_mounted = true;

        // Test basic file operations
        ESP_LOGI(TAG, "Testing file operations...");

        // Try to list files
        DIR *dir = opendir("/sdcard");
        if (dir) {
            struct dirent *entry;
            int file_count = 0;
            ESP_LOGI(TAG, "Files on SD card:");
            while ((entry = readdir(dir)) != NULL) {
                ESP_LOGI(TAG, "  📄 %s", entry->d_name);
                file_count++;
            }
            closedir(dir);
            ESP_LOGI(TAG, "Total files found: %d", file_count);
        } else {
            ESP_LOGI(TAG, "Failed to open directory");
        }

        // Try to write a test file
        const char *test_file = "/sdcard/test.txt";
        FILE *f = fopen(test_file, "w");
        if (f) {
            fputs("Esposito OS SD Card Test - SUCCESS!", f);
            fclose(f);
            ESP_LOGI(TAG, "✅ Successfully wrote test file");

            // Try to read it back
            char buffer[128];
            f = fopen(test_file, "r");
            if (f) {
                if (fgets(buffer, sizeof(buffer), f)) {
                    ESP_LOGI(TAG, "✅ Read back: %s", buffer);
                } else {
                    ESP_LOGI(TAG, "✅ Read back: (empty)");
                }
                fclose(f);

                // Clean up test file
                remove(test_file);
                ESP_LOGI(TAG, "✅ Test file removed");
            }
        } else {
            ESP_LOGI(TAG, "Failed to create test file");
        }

        return true;
    }

    ESP_LOGE(TAG, "❌ SDSPI mount failed: %s (0x%x)", esp_err_to_name(ret), ret);
    ESP_LOGE(TAG, "");
    ESP_LOGE(TAG, "Possible issues:");
    ESP_LOGE(TAG, "- Wrong pin configuration");
    ESP_LOGE(TAG, "- SD card not inserted");
    ESP_LOGE(TAG, "- SD card not formatted as FAT/FAT32");
    ESP_LOGE(TAG, "- Hardware issue with SD card slot");
    ESP_LOGE(TAG, "");
    ESP_LOGE(TAG, "Next troubleshooting steps:");
    ESP_LOGI(TAG, "1. Verify SD card is inserted properly");
    ESP_LOGI(TAG, "2. Try reformatting SD card as FAT32");
    ESP_LOGI(TAG, "3. Check if SD card works in other devices");

    return false;
}

bool sd_card_is_mounted(void) {
    return sd_card_mounted;
}

const char* sd_card_get_mount_point(void) { return "/sdcard"; }

// Build a fully-qualified path under /sdcard into out (size out_size).
// If path starts with '/', it is treated as already-qualified and copied as-is.
// Otherwise it is joined under "/sdcard".
// Always null-terminates. Returns false on truncation or NULL input.
static bool build_full_path(const char *path, char *out, size_t out_size) {
    if (!path || !out || out_size == 0) return false;
    int n;
    if (path[0] == '/') {
        n = snprintf(out, out_size, "%s", path);
    } else {
        n = snprintf(out, out_size, "%s/%s", "/sdcard", path);
    }
    if (n < 0 || (size_t)n >= out_size) {
        // Truncated. Still null-terminate for safety.
        out[out_size - 1] = '\0';
        return false;
    }
    return true;
}

bool sd_card_list_files(const char *path) {
    char full_path[128];
    if (!build_full_path(path, full_path, sizeof(full_path))) {
        ESP_LOGE(TAG, "Path too long");
        return false;
    }

    DIR *dir = opendir(full_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", full_path);
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        ESP_LOGI(TAG, "  %s", entry->d_name);
    }
    closedir(dir);
    return true;
}

bool sd_card_read_file(const char *path, char *buffer, size_t max_len) {
    if (!buffer || max_len == 0) return false;

    char full_path[128];
    if (!build_full_path(path, full_path, sizeof(full_path))) {
        ESP_LOGE(TAG, "Path too long");
        return false;
    }

    FILE *f = fopen(full_path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", full_path);
        return false;
    }

    size_t bytes_read = fread(buffer, 1, max_len - 1, f);
    buffer[bytes_read] = '\0';
    fclose(f);
    return true;
}

bool sd_card_write_file(const char *path, const char *data) {
    char full_path[128];
    if (!build_full_path(path, full_path, sizeof(full_path))) {
        ESP_LOGE(TAG, "Path too long");
        return false;
    }

    FILE *f = fopen(full_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", full_path);
        return false;
    }

    fprintf(f, "%s", data);
    fclose(f);
    return true;
}

void sd_card_unmount(void) {
    if (sd_card_mounted) {
        esp_vfs_fat_sdcard_unmount("/sdcard", NULL);
        sd_card_mounted = false;
        ESP_LOGI(TAG, "SD card unmounted");
    }
}

#else // BOARD_HAS_SD_CARD

bool sd_card_init(void) {
    ESP_LOGI(TAG, "No SD card on this board");
    return false;
}

bool sd_card_is_mounted(void) {
    return false;
}

const char* sd_card_get_mount_point(void) { return "/sdcard"; }

bool sd_card_list_files(const char *path) {
    (void)path;
    return false;
}

bool sd_card_read_file(const char *path, char *buffer, size_t max_len) {
    (void)path; (void)buffer; (void)max_len;
    return false;
}

bool sd_card_write_file(const char *path, const char *data) {
    (void)path; (void)data;
    return false;
}

void sd_card_unmount(void) {
}

#endif // BOARD_HAS_SD_CARD
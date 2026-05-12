#include "sd_card.h"
#include "hardware_config.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include <string.h>
#include <dirent.h>

static const char *TAG = "sd_card_test";

static bool sd_card_mounted = false;

bool sd_card_init(void) {
    ESP_LOGI(TAG, "Initializing SD card on SPI3/VSPI bus");
    ESP_LOGI(TAG, "Using CYD2USB SDSPI mode with SPI3/VSPI bus (separate from display):");
    ESP_LOGI(TAG, "  MISO=%d, MOSI=%d, SCLK=%d, CS=%d", SD_MISO_PIN, SD_MOSI_PIN, SD_CLK_PIN, SD_CS_PIN);

    esp_err_t ret;

    // Try SDSPI mode (SPI mode - like Arduino SD library)
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;

    // Configure SDSPI host to use SPI3_HOST (VSPI, separate bus from display)
    // The CYD2USB uses VSPI for SD card (default ESP32 pins: MISO=19, MOSI=23, SCLK=18)

    // First, we need to initialize the SPI3/VSPI bus (not done automatically)
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_PIN,
        .miso_io_num = SD_MISO_PIN,
        .sclk_io_num = SD_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4092,
    };

    ESP_LOGI(TAG, "Initializing SPI3/VSPI bus...");
    ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;  // Use SPI3/VSPI bus (separate from display's SPI2/HSPI)
    host.max_freq_khz = 20000;  // Start with lower frequency

    ESP_LOGI(TAG, "Attempting SDSPI mount on SPI3/VSPI bus...");

    // Configure SDSPI device - use SPI3/VSPI bus
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN;
    slot_config.host_id = SPI3_HOST;  // SPI3/VSPI bus, separate from display

    ret = esp_vfs_fat_sdspi_mount(
        "/sdcard",
        &host,
        &slot_config,
        &mount_config,
        &card
    );

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ SUCCESS! SD card mounted with SDSPI mode on SPI3/VSPI bus");
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
                fgets(buffer, sizeof(buffer), f);
                fclose(f);
                ESP_LOGI(TAG, "✅ Read back: %s", buffer);

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

bool sd_card_list_files(const char *path) {
    char full_path[128];
    if (path[0] == '/') {
        strncpy(full_path, path, sizeof(full_path) - 1);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", "/sdcard", path);
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
    if (path[0] == '/') {
        strncpy(full_path, path, sizeof(full_path) - 1);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", "/sdcard", path);
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
    if (path[0] == '/') {
        strncpy(full_path, path, sizeof(full_path) - 1);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", "/sdcard", path);
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
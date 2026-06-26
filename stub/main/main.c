#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "board.h"
#include <dirent.h>

static const char *TAG = "stub";
static sdmmc_card_t *sd_card = NULL;

#define SD_MISO_PIN BOARD_SD_MISO_PIN
#define SD_MOSI_PIN BOARD_SD_MOSI_PIN
#define SD_CLK_PIN  BOARD_SD_CLK_PIN
#define SD_CS_PIN   BOARD_SD_CS_PIN
#define SD_SPI_HOST BOARD_SD_SPI_HOST

static bool mount_sd_card(void) {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_PIN,
        .miso_io_num = SD_MISO_PIN,
        .sclk_io_num = SD_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4092,
    };

    esp_err_t ret = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN;
    slot_config.host_id = SD_SPI_HOST;

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        spi_bus_free(SD_SPI_HOST);
        return false;
    }

    ESP_LOGI(TAG, "SD card mounted");
    return true;
}

static void unmount_sd_card(void) {
    esp_vfs_fat_sdcard_unmount("/sdcard", sd_card);
    spi_bus_free(SD_SPI_HOST);
}

static void fallback_to_factory(void) {
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory) {
        esp_ota_set_boot_partition(factory);
    }
    esp_restart();
}

void app_main(void) {
    ESP_LOGI(TAG, "=== Esposito Update Stub ===");

    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!factory) {
        ESP_LOGE(TAG, "Factory partition not found!");
        esp_restart();
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running from partition at 0x%x, subtype %d",
             running->address, running->subtype);

    if (running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        ESP_LOGW(TAG, "Not running from update partition, falling back to factory");
        fallback_to_factory();
        return;
    }

    if (!mount_sd_card()) {
        ESP_LOGE(TAG, "SD card mount failed, falling back to factory");
        fallback_to_factory();
        return;
    }

    FILE *fp = fopen("/sdcard/system/firmware.bin", "rb");
    if (!fp) {
        ESP_LOGI(TAG, "No firmware.bin found on SD, booting factory");
        unmount_sd_card();
        fallback_to_factory();
        return;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || (size_t)file_size > factory->size) {
        ESP_LOGE(TAG, "Invalid firmware size: %ld (partition: %u)",
                 file_size, factory->size);
        fclose(fp);
        unmount_sd_card();
        fallback_to_factory();
        return;
    }

    ESP_LOGI(TAG, "Found firmware.bin: %ld bytes", file_size);
    ESP_LOGI(TAG, "Erasing factory partition...");

    esp_err_t err = esp_partition_erase_range(factory, 0, factory->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erase failed: %s", esp_err_to_name(err));
        fclose(fp);
        unmount_sd_card();
        fallback_to_factory();
        return;
    }

    ESP_LOGI(TAG, "Writing firmware to factory partition...");

    char buffer[4096];
    size_t total = 0;
    int last_pct = -1;

    while (total < (size_t)file_size) {
        size_t to_read = sizeof(buffer);
        if (to_read > (size_t)(file_size - (long)total)) {
            to_read = (size_t)(file_size - (long)total);
        }

        size_t read_len = fread(buffer, 1, to_read, fp);
        if (read_len == 0) break;

        err = esp_partition_write(factory, total, buffer, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Write failed at %zu: %s", total, esp_err_to_name(err));
            fclose(fp);
            unmount_sd_card();
            fallback_to_factory();
            return;
        }

        total += read_len;
        int pct = (int)(total * 100 / file_size);
        if (pct != last_pct && pct % 10 == 0) {
            last_pct = pct;
            ESP_LOGI(TAG, "Writing: %d%%", pct);
        }
    }

    fclose(fp);

    if (total != (size_t)file_size) {
        ESP_LOGE(TAG, "Write incomplete: %zu/%ld", total, file_size);
        unmount_sd_card();
        fallback_to_factory();
        return;
    }

    ESP_LOGI(TAG, "Wrote %zu bytes to factory partition", total);

    remove("/sdcard/system/firmware.bin");
    ESP_LOGI(TAG, "Deleted firmware.bin from SD");

    unmount_sd_card();

    ESP_LOGI(TAG, "Update complete! Setting boot to factory and rebooting...");

    err = esp_ota_set_boot_partition(factory);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
    }

    esp_restart();
}

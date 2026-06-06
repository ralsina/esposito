idf_component_register(
    SRCS "main.c"
    REQUIRES driver spi_flash sdmmc fatfs esp_partition app_update
)

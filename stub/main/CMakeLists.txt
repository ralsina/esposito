# Pick the same board as the main firmware (see main/CMakeLists.txt).
if(DEFINED BOARD)
    set(ESP_BOARD "${BOARD}")
elseif(IDF_TARGET STREQUAL "esp32c3")
    set(ESP_BOARD "elecrow_round")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(ESP_BOARD "guition_jc4827w543")
else()
    set(ESP_BOARD "cyd_2usb")
endif()

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "../../boards/${ESP_BOARD}"
    REQUIRES driver spi_flash sdmmc fatfs esp_partition app_update
)

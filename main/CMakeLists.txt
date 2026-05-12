idf_component_register(
    SRCS "main.c"
        "os_core.c"
        "os_symtab.c"
        "elf_loader.c"
        "app_loader.c"
        "app_launcher.c"
        "checkpoint.c"
        "hardware.cpp"
        "boot.cpp"
        "bbq20_keyboard.c"
        "sd_card.c"
        "touchscreen.c"
        "text_mode.cpp"
        "wifi.cpp"
    INCLUDE_DIRS "." ".."
    REQUIRES driver esp_app_format esp_common esp_hw_support esp_system heap log pthread xtensa spiffs esp_driver_spi cxx esp_driver_i2c esp_driver_sdspi esp_driver_uart fatfs wear_levelling esp_wifi esp_netif nvs_flash esp_event
)

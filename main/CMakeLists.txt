idf_component_register(
    SRCS "main.c"
        "os_core.c"
        "app_loader.c"
        "checkpoint.c"
        "hardware.cpp"
        "boot.cpp"
        "bbq20_keyboard.c"
        "../apps/key_echo/app.c"
    INCLUDE_DIRS "." ".."
    REQUIRES driver esp_app_format esp_common esp_hw_support esp_system heap log pthread xtensa spiffs esp_driver_spi cxx esp_driver_i2c
)

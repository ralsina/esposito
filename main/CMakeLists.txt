if(IDF_TARGET STREQUAL "esp32c3")
    set(ARCH_DEPS riscv)
    set(BOARD_PATH "${CMAKE_CURRENT_LIST_DIR}/../boards/elecrow_round")
else()
    set(ARCH_DEPS xtensa)
    set(BOARD_PATH "${CMAKE_CURRENT_LIST_DIR}/../boards/cyd_2usb")
endif()

# Collect Lua source files from libs/lua/
file(GLOB LUA_SOURCES "../libs/lua/*.c")

idf_component_register(
    SRCS "main.c"
        "app_heap.c"
        "app_config.c"
        "app_manifest.c"
        "credential_store.c"
        "os_core.c"
        "os_symtab.c"
        "os_printf.c"
        "elf_loader.c"
        "app_loader.c"
        "app_launcher.c"
        "ota_update.c"
        "hardware.cpp"
        "boot.cpp"
        "bbq20_keyboard.c"
        "sd_card.c"
        "touchscreen.c"
        "terminal_vt100.c"
        "terminal_mode.c"
        "text_mode.cpp"
        "graphics_mode.cpp"
        "wifi.cpp"
        "../fonts/fonts.cpp"
        "../libs/json/core_json.c"
        ${LUA_SOURCES}
    INCLUDE_DIRS "." ".." "../libs/ui" "../libs/json" "../fonts" "${BOARD_PATH}" "../managed_components/LovyanGFX/src/lgfx/utility" "../libs/lua"
    REQUIRES spi_flash driver esp_app_format esp_common esp_hw_support esp_system heap log pthread ${ARCH_DEPS} spiffs esp_driver_spi cxx esp_driver_i2c esp_driver_sdspi esp_driver_uart fatfs wear_levelling esp_wifi esp_netif nvs_flash esp_event lwip esp_http_client mbedtls app_update
)

# Lua configuration for ESP32 (32-bit integers and floats required)
# LUA_32BITS is defined in libs/lua/luaconf.h
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-sign-compare)

# Get firmware version from git
execute_process(
    COMMAND git describe --always --dirty --tags
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE FIRMWARE_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT FIRMWARE_VERSION)
    set(FIRMWARE_VERSION "unknown")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE FIRMWARE_VERSION="${FIRMWARE_VERSION}")

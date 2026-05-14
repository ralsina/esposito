#!/bin/bash
# Generate OS symbol table linker script for app ELF builds
# Usage: gen_symtab.sh <firmware_elf> <output_ld>

FIRMWARE_ELF="${1:-build/esposito.elf}"
OUTPUT_LD="${2:-build/os_symbols.ld}"

if [ ! -f "$FIRMWARE_ELF" ]; then
    echo "Error: firmware ELF not found: $FIRMWARE_ELF"
    echo "Usage: $0 [firmware_elf] [output_ld]"
    exit 1
fi

TOOLCHAIN_PREFIX="${TOOLCHAIN_PREFIX:-xtensa-esp32-elf}"

# Symbols that apps can import from the OS
# These match the entries in main/os_symtab.c
SYMBOLS=(
    display_clear
    display_draw_text
    display_draw_text_bg
    display_draw_pixel
    display_fill_rect
    display_draw_char_at
    keyboard_read_event
    checkpoint_save_string
    checkpoint_load_string
    checkpoint_save_int
    checkpoint_load_int
    checkpoint_save
    checkpoint_open
    checkpoint_close
    os_get_current_app
    app_launcher_start
    text_mode_init
    text_mode_init_ex
    text_mode_get_cols
    text_mode_get_rows
    text_mode_get_char_width
    text_mode_get_char_height
    text_mode_get_font
    text_mode_clear
    text_mode_print_at
    text_mode_print_at_color
    text_mode_printf_at
    text_mode_printf_at_color
    text_mode_print_at_attr
    text_mode_printf_at_attr
    text_mode_print_at_attr_bg
    text_mode_printf_at_attr_bg
    text_mode_get_cursor
    text_mode_set_cursor
    text_mode_save
    text_mode_restore
    text_mode_flush
    printf
    puts
    sprintf
    snprintf
    memset
    memcpy
    memmove
    strlen
    strcmp
    strncmp
    strcpy
    strncpy
    strcat
    strchr
    strrchr
    strstr
    malloc:app_malloc
    calloc:app_calloc
    realloc:app_realloc
    free:app_free
    atoi
    atol
    abs
    opendir
    readdir
    closedir
    stat
    os_log
    serial_init
    serial_write
    wifi_init
    wifi_is_connected
    wifi_get_ip
    wifi_scan
    wifi_scan_get_ssid
    wifi_scan_get_rssi
    wifi_connect
    wifi_disconnect
    wifi_save_config
    font_table
    font_lookup_by_name
    fopen
    fread
    fwrite
    fclose
    fseek
    ftell
    fgets
    config_open_read
    config_open_write
    config_exists
    config_delete
    config_read_all_alloc
    config_free
    config_get_int
    config_get_float
    config_get_bool
    config_get_string
    config_set_int
    config_set_float
    config_set_bool
    config_set_string
)

echo "/* Auto-generated OS symbol table for app linking */" > "$OUTPUT_LD"
echo "/* Generated from: $FIRMWARE_ELF */" >> "$OUTPUT_LD"
echo "" >> "$OUTPUT_LD"

for sym_spec in "${SYMBOLS[@]}"; do
    export_name="$sym_spec"
    provider_name="$sym_spec"
    if [[ "$sym_spec" == *:* ]]; then
        export_name="${sym_spec%%:*}"
        provider_name="${sym_spec##*:}"
    fi

    addr=$("${TOOLCHAIN_PREFIX}-nm" "$FIRMWARE_ELF" 2>/dev/null | grep " [TDWAi] $provider_name$" | head -1 | awk '{print $1}')
    if [ -n "$addr" ]; then
        echo "PROVIDE($export_name = 0x$addr);" >> "$OUTPUT_LD"
    else
        echo "/* WARNING: $export_name ($provider_name) not found in firmware ELF */" >> "$OUTPUT_LD"
        echo "PROVIDE($export_name = 0);" >> "$OUTPUT_LD"
    fi
done

echo "" >> "$OUTPUT_LD"
echo "/* End of OS symbol table */" >> "$OUTPUT_LD"
echo "Generated $OUTPUT_LD with $(grep -c '^PROVIDE' "$OUTPUT_LD") symbols"

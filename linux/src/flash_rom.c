#include "hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *rom_data = NULL;
static size_t rom_size = 0;

const uint8_t *flash_rom_load(const char *path, size_t *out_size) {
    flash_rom_unload();

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "flash_rom_load: can't open %s\n", path);
        if (out_size) *out_size = 0;
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        if (out_size) *out_size = 0;
        return NULL;
    }

    rom_data = malloc(size);
    if (!rom_data) {
        fclose(f);
        if (out_size) *out_size = 0;
        return NULL;
    }

    size_t nread = fread(rom_data, 1, size, f);
    fclose(f);

    if (nread != (size_t)size) {
        free(rom_data);
        rom_data = NULL;
        if (out_size) *out_size = 0;
        return NULL;
    }

    rom_size = size;
    if (out_size) *out_size = size;
    return rom_data;
}

void flash_rom_unload(void) {
    free(rom_data);
    rom_data = NULL;
    rom_size = 0;
}

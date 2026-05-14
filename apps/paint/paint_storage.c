#include "paint_storage.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char magic[4];
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t reserved;
} paint_file_header_t;

bool paint_storage_save(const paint_state_t *state, const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }

    paint_file_header_t header;
    memcpy(header.magic, "PT16", 4);
    header.version = 1;
    header.width = PAINT_WIDTH;
    header.height = PAINT_HEIGHT;
    header.reserved = 0;

    size_t written = fwrite(&header, 1, sizeof(header), file);
    if (written != sizeof(header)) {
        fclose(file);
        return false;
    }

    written = fwrite(state->canvas, 1, PAINT_CANVAS_BYTES, file);
    fclose(file);
    return written == PAINT_CANVAS_BYTES;
}

bool paint_storage_load(paint_state_t *state, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }

    paint_file_header_t header;
    size_t read = fread(&header, 1, sizeof(header), file);
    if (read != sizeof(header)) {
        fclose(file);
        return false;
    }

    if (strncmp(header.magic, "PT16", 4) != 0 ||
        header.version != 1 ||
        header.width != PAINT_WIDTH ||
        header.height != PAINT_HEIGHT) {
        fclose(file);
        return false;
    }

    read = fread(state->canvas, 1, PAINT_CANVAS_BYTES, file);
    fclose(file);
    return read == PAINT_CANVAS_BYTES;
}

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
        printf("paint_storage_save: failed to open %s for writing\n", path);
        return false;
    }

    paint_file_header_t header;
    memcpy(header.magic, "PT16", 4);
    header.version = 1;
    header.width = display_get_width();
    header.height = display_get_height();
    header.reserved = 0;

    size_t written = fwrite(&header, 1, sizeof(header), file);
    if (written != sizeof(header)) {
        printf("paint_storage_save: header write failed (%u/%u)\n", written, sizeof(header));
        fclose(file);
        return false;
    }

    written = fwrite(state->canvas, 1, paint_get_canvas_bytes(), file);
    fclose(file);
    printf("paint_storage_save: wrote %u/%u bytes to %s\n", written, paint_get_canvas_bytes(), path);
    return written == paint_get_canvas_bytes();
}

bool paint_storage_load(paint_state_t *state, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        printf("paint_storage_load: failed to open %s (no saved project)\n", path);
        return false;
    }

    paint_file_header_t header;
    size_t read = fread(&header, 1, sizeof(header), file);
    if (read != sizeof(header)) {
        printf("paint_storage_load: header read failed (%u/%u)\n", read, sizeof(header));
        fclose(file);
        return false;
    }

    if (strncmp(header.magic, "PT16", 4) != 0 ||
        header.version != 1 ||
        header.width != display_get_width() ||
        header.height != display_get_height()) {
        printf("paint_storage_load: header mismatch (magic=%.4s v=%d %dx%d vs %dx%d)\n",
               header.magic, header.version, header.width, header.height,
               display_get_width(), display_get_height());
        fclose(file);
        return false;
    }

    read = fread(state->canvas, 1, paint_get_canvas_bytes(), file);
    fclose(file);
    printf("paint_storage_load: read %u/%u bytes from %s\n", read, paint_get_canvas_bytes(), path);
    return read == paint_get_canvas_bytes();
}

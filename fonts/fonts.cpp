#include "fonts.h"
#include <lgfx/utility/pgmspace.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_partition.h"
#include "esp_log.h"

static const char *TAG = "fonts";

// Embedded boot font (hack regular 6px, ~15 KB PROGMEM)
#include "boot_font.h"

// Static boot font data for initial use (before font_cache_init)
// The name "boot 6" keeps it distinct from SD-loaded fonts
static const font_info_t boot_font_info = {
    .id = FONT_BOOT,
    .name = "boot 6",
    .family = "boot",
    .size = 6,
    .char_width = 6,
    .char_height = 10,
};

// Static initial table so font_table is always valid (even before SD init)
static font_info_t initial_font_table[] = { boot_font_info };

font_info_t *font_table = initial_font_table;
int font_count = 1;

// Cached font state
static const esp_partition_t *cache_partition = NULL;
static esp_partition_mmap_handle_t cache_mmap_handle;
static const uint8_t *cache_mmap_ptr = NULL;
static int cached_font_id = -1;  // -1 = nothing cached; 0 always stays cached

// FPACK header parsing
#define FPACK_MAGIC   0x4650414B
#define FPACK_VERSION 1
#define FPACK_HEADER_SIZE 116

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    char     name[32];
    char     family[32];
    uint8_t  size;
    uint8_t  char_width;
    uint8_t  char_height;
    uint8_t  _reserved;
    uint32_t variant_offsets[4];
    uint32_t variant_sizes[4];
    uint32_t supplement_offset;
    uint32_t supplement_size;
} fpack_header_t;
#pragma pack(pop)

static bool read_be32(const uint8_t *p, uint32_t *out) {
    *out = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
    return true;
}

static bool read_fpack_header(const char *path, fpack_header_t *hdr) {
    uint8_t raw[FPACK_HEADER_SIZE];
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = fread(raw, 1, sizeof(raw), f) == sizeof(raw);
    fclose(f);
    if (!ok) return false;

    uint32_t magic;
    read_be32(raw + 0, &magic);
    if (magic != FPACK_MAGIC) return false;

    uint32_t version;
    read_be32(raw + 4, &version);
    if (version != FPACK_VERSION) return false;

    // Byteswap all multi-byte fields (header is big-endian on disk)
    hdr->magic = magic;
    hdr->version = version;
    memcpy(hdr->name, raw + 8, 32);
    memcpy(hdr->family, raw + 40, 32);
    hdr->size = raw[72];
    hdr->char_width = raw[73];
    hdr->char_height = raw[74];
    hdr->_reserved = raw[75];
    for (int i = 0; i < 4; i++) {
        read_be32(raw + 76 + i * 8, &hdr->variant_offsets[i]);
        read_be32(raw + 80 + i * 8, &hdr->variant_sizes[i]);
    }
    read_be32(raw + 108, &hdr->supplement_offset);
    read_be32(raw + 112, &hdr->supplement_size);
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

font_id_t font_lookup_by_name(const char *name) {
    if (!name || !font_table) return -1;
    for (int i = 0; i < font_count; i++) {
        const char *fn = font_table[i].name;
        int j = 0;
        while (fn[j] && name[j]) {
            char a = fn[j], b = name[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            j++;
        }
        if (fn[j] == '\0' && name[j] == '\0')
            return font_table[i].id;
    }
    return -1;
}

bool font_get_vlw_metrics(const uint8_t *data, size_t size, int *out_width, int *out_height) {
    if (!data || size < 24) return false;

    uint32_t glyph_count, ascent, descent, y_advance;
    read_be32(data + 0, &glyph_count);
    read_be32(data + 4, &y_advance);
    read_be32(data + 8, &ascent);
    read_be32(data + 12, &descent);

    if (glyph_count <= 0 || glyph_count > 1000) return false;

    int a = (int)ascent;
    int d = (int)descent;
    if (y_advance <= 0) y_advance = (uint32_t)(a + d);
    int height = (int)(y_advance > (uint32_t)(a + d) ? y_advance : (uint32_t)(a + d));
    if (height <= 0) height = 9;

    int max_width = 0;
    int max_advance = 0;
    for (uint32_t i = 0; i < glyph_count; i++) {
        int off = 24 + (int)i * 28;
        if (off + 20 > (int)size) break;
        uint32_t glyph_width, x_advance;
        read_be32(data + off + 8, &glyph_width);
        read_be32(data + off + 12, &x_advance);
        if ((int)glyph_width > max_width && glyph_width < 100) max_width = (int)glyph_width;
        if ((int)x_advance > max_advance && x_advance < 100) max_advance = (int)x_advance;
    }

    int width = max_width > 0 ? max_width : (max_advance > 0 ? max_advance : 6);
    if (width <= 0) width = 6;

    if (out_width) *out_width = width;
    if (out_height) *out_height = height;

    ESP_LOGI(TAG, "VLW metrics: width=%d, height=%d (ascent=%d, descent=%d, yAdvance=%u, max_glyph_width=%d)",
             width, height, a, d, y_advance, max_width);

    return true;
}

bool font_cache_init(void) {
    // Count .fpack files on SD
    int sd_count = 0;
    DIR *dir = opendir("/sdcard/fonts/fpack");
    if (!dir) {
        ESP_LOGW(TAG, "No /sdcard/fonts/fpack directory, using boot font only");
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strstr(entry->d_name, ".fpack")) sd_count++;
    }
    closedir(dir);

    if (sd_count == 0) {
        ESP_LOGW(TAG, "No .fpack files found, using boot font only");
        return true;
    }

    ESP_LOGI(TAG, "Found %d font packs on SD", sd_count);

    // Allocate font_table: boot + all discovered fonts
    int total = 1 + sd_count;
    font_info_t *table = (font_info_t *)calloc((size_t)total, sizeof(font_info_t));
    if (!table) {
        ESP_LOGE(TAG, "Failed to allocate font_table");
        return false;
    }

    // Copy boot font entry
    table[0] = boot_font_info;

    // Fill in SD font entries
    dir = opendir("/sdcard/fonts/fpack");
    int idx = 1;
    while ((entry = readdir(dir)) && idx < total) {
        if (!strstr(entry->d_name, ".fpack")) continue;

        char path[300];
        snprintf(path, sizeof(path), "/sdcard/fonts/fpack/%s", entry->d_name);

        fpack_header_t hdr;
        if (!read_fpack_header(path, &hdr)) {
            ESP_LOGW(TAG, "Skipping invalid fpack: %s", entry->d_name);
            continue;
        }

        // Ensure name is null-terminated
        hdr.name[31] = '\0';
        hdr.family[31] = '\0';

        char *name_copy = strdup((const char *)hdr.name);
        char *family_copy = strdup((const char *)hdr.family);
        if (!name_copy || !family_copy) {
            free(name_copy);
            free(family_copy);
            continue;
        }

        table[idx].id = idx;
        table[idx].name = name_copy;
        table[idx].family = family_copy;
        table[idx].size = (int)hdr.size;
        table[idx].char_width = (int)hdr.char_width;
        table[idx].char_height = (int)hdr.char_height;

        ESP_LOGI(TAG, "Loaded font: %s (family=%s, size=%d, fpack_width=%d, fpack_height=%d)",
                 table[idx].name, table[idx].family, table[idx].size,
                 table[idx].char_width, table[idx].char_height);

        idx++;
    }
    closedir(dir);

    font_count = idx;
    font_table = table;

    // Look up the fontcache flash partition
    cache_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                               ESP_PARTITION_SUBTYPE_ANY, "fontcache");
    if (!cache_partition) {
        ESP_LOGE(TAG, "fontcache partition not found - SD fonts unavailable");
    }

    ESP_LOGI(TAG, "Font cache initialized: %d fonts available", font_count);
    return true;
}

static bool load_fpack_to_cache(int id) {
    if (id < 0 || id >= font_count) return false;
    if (id == FONT_BOOT) {
        // Boot font is always in PROGMEM, no SD load needed
        cached_font_id = FONT_BOOT;
        return true;
    }

    const font_info_t *info = &font_table[id];
    if (!info->name) return false;

    // Build SD path: /sdcard/fonts/fpack/<family>-<size>.fpack
    // name is e.g. "hack 10", family is "hack", size is 10
    char path[128];
    snprintf(path, sizeof(path), "/sdcard/fonts/fpack/%s-%d.fpack", info->family, info->size);

    // Open the .fpack file from SD
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open fpack: %s (errno=%d)", path, errno);
        return false;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    size_t file_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < FPACK_HEADER_SIZE) {
        ESP_LOGE(TAG, "Truncated fpack: %s", path);
        fclose(f);
        return false;
    }

    // Round up file size to 4 KB (minimum erase block)
    uint32_t aligned_size = (file_size + 0xFFF) & ~0xFFF;
    uint32_t partition_size = cache_partition->size;

    if (aligned_size > partition_size) {
        ESP_LOGE(TAG, "fpack %s too large (%u bytes) for fontcache partition (%u bytes)",
                 path, aligned_size, partition_size);
        fclose(f);
        return false;
    }

    // Erase the fontcache partition
    esp_err_t err = esp_partition_erase_range(cache_partition, 0, aligned_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase fontcache partition: %s", esp_err_to_name(err));
        fclose(f);
        return false;
    }

    // Write the fpack file to the partition in 4 KB chunks
    uint8_t buf[4096];
    size_t remaining = file_size;
    uint32_t offset = 0;
    while (remaining > 0) {
        size_t to_read = remaining < sizeof(buf) ? remaining : sizeof(buf);
        if (fread(buf, 1, to_read, f) != to_read) {
            ESP_LOGE(TAG, "Read error from %s", path);
            fclose(f);
            return false;
        }
        // Zero out the rest of the buffer (partial last chunk)
        if (to_read < sizeof(buf)) {
            memset(buf + to_read, 0, sizeof(buf) - to_read);
        }
        err = esp_partition_write(cache_partition, offset, buf, sizeof(buf));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write fontcache partition: %s", esp_err_to_name(err));
            fclose(f);
            return false;
        }
        offset += sizeof(buf);
        remaining -= to_read;
    }
    fclose(f);

    // Unmap previous cache if any
    if (cache_mmap_ptr) {
        esp_partition_munmap(cache_mmap_handle);
        cache_mmap_ptr = NULL;
    }

    // Memory-map the partition
    err = esp_partition_mmap(cache_partition, 0, file_size, ESP_PARTITION_MMAP_DATA,
                             (const void **)&cache_mmap_ptr, &cache_mmap_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mmap fontcache: %s", esp_err_to_name(err));
        cache_mmap_ptr = NULL;
        return false;
    }

    cached_font_id = id;
    ESP_LOGI(TAG, "Cached font %s to flash partition (%zu bytes)", info->name, file_size);
    return true;
}

const uint8_t *font_get_variant_data(font_id_t id, font_variant_t variant, size_t *out_size) {
    if (id < 0 || id >= font_count) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    if (variant < 0 || variant >= FONT_VARIANT_COUNT) variant = FONT_VARIANT_REGULAR;

    // Boot font is always available in PROGMEM (regular variant only)
    if (id == FONT_BOOT) {
        // Return the embedded boot font VLW data
        if (variant == FONT_VARIANT_REGULAR) {
            if (out_size) *out_size = sizeof(hack_6);
            return (const uint8_t *)hack_6;
        }
        // Fallback to regular for missing variants
        if (out_size) *out_size = sizeof(hack_6);
        return (const uint8_t *)hack_6;
    }

    // For SD fonts, ensure the font is cached in the flash partition
    if (id != cached_font_id) {
        if (!load_fpack_to_cache(id)) {
            if (out_size) *out_size = 0;
            return NULL;
        }
    }

    if (!cache_mmap_ptr) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    // Read variant offset/size from the mmap'd region (big-endian header)
    uint32_t offset = 0, size = 0;
    read_be32(cache_mmap_ptr + 76 + (int)variant * 8, &offset);
    read_be32(cache_mmap_ptr + 80 + (int)variant * 8, &size);

    if (offset == 0 || size == 0) {
        if (variant != FONT_VARIANT_REGULAR) {
            read_be32(cache_mmap_ptr + 76 + (int)FONT_VARIANT_REGULAR * 8, &offset);
            read_be32(cache_mmap_ptr + 80 + (int)FONT_VARIANT_REGULAR * 8, &size);
            if (offset != 0 && size != 0) {
                if (out_size) *out_size = (size_t)size;
                return cache_mmap_ptr + offset;
            }
        }
        if (out_size) *out_size = 0;
        return NULL;
    }

    if (out_size) *out_size = (size_t)size;
    return cache_mmap_ptr + offset;
}

const uint8_t *font_get_supplement_data(font_id_t id, size_t *out_size) {
    if (id < 0 || id >= font_count) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    // Boot font has no supplement
    if (id == FONT_BOOT) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    // Ensure the font is cached
    if (id != cached_font_id) {
        if (!load_fpack_to_cache(id)) {
            if (out_size) *out_size = 0;
            return NULL;
        }
    }

    if (!cache_mmap_ptr) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    uint32_t offset = 0, size = 0;
    read_be32(cache_mmap_ptr + 108, &offset);
    read_be32(cache_mmap_ptr + 112, &size);

    if (offset == 0 || size == 0) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    if (out_size) *out_size = (size_t)size;
    return cache_mmap_ptr + offset;
}

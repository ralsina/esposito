#include "text_mode.h"
#include <SDL2/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- VLW glyph cache ---
typedef struct {
    uint16_t codepoint;
    int width, height;
    int top_offset, left_offset;
    uint8_t *bitmap;       // width * height 8-bit alpha
    SDL_Texture *texture;  // cached ARGB8888 texture (white pixels, alpha from bitmap)
} vlw_glyph_t;

static vlw_glyph_t *vlw_glyphs = NULL;
static int vlw_glyph_count = 0;
static int glyph_advance = 0;
static int glyph_ascent = 0;
static int glyph_descent = 0;

static void vlw_unload(void) {
    if (vlw_glyphs) {
        for (int i = 0; i < vlw_glyph_count; i++) {
            free(vlw_glyphs[i].bitmap);
            if (vlw_glyphs[i].texture)
                SDL_DestroyTexture(vlw_glyphs[i].texture);
        }
        free(vlw_glyphs);
        vlw_glyphs = NULL;
    }
    vlw_glyph_count = 0;
}

static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static int32_t rd32be_s(const uint8_t *p) {
    return (int32_t)rd32be(p);
}

static bool vlw_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(file_size);
    if (!data || fread(data, 1, file_size, f) != (size_t)file_size) {
        free(data); fclose(f); return false;
    }
    fclose(f);

    uint32_t glyph_count = rd32be(data + 0);
    uint32_t ascent = rd32be(data + 16);
    uint32_t descent = rd32be(data + 20);

    glyph_ascent = (int)ascent;
    glyph_descent = (int)descent;

    // Frequency map of advance widths to find the modal value
    int adv_freq[256] = {0};

    vlw_glyph_count = (int)glyph_count;
    vlw_glyphs = calloc(glyph_count, sizeof(vlw_glyph_t));
    if (!vlw_glyphs) { free(data); return false; }

    uint32_t bitmap_offset = 24 + glyph_count * 28;

    for (uint32_t i = 0; i < glyph_count; i++) {
        uint32_t off = 24 + i * 28;
        vlw_glyph_t *g = &vlw_glyphs[i];
        g->codepoint = rd32be(data + off);
        uint32_t h = rd32be(data + off + 4);
        uint32_t w = rd32be(data + off + 8);
        uint32_t adv = rd32be(data + off + 12);
        g->top_offset = rd32be_s(data + off + 16);
        g->left_offset = rd32be_s(data + off + 20);
        g->width = (int)w;
        g->height = (int)h;
        g->bitmap = NULL;
        g->texture = NULL;

        if (w > 0 && h > 0) {
            g->bitmap = malloc(w * h);
            if (g->bitmap)
                memcpy(g->bitmap, data + bitmap_offset, w * h);
            bitmap_offset += w * h;
        }

        if (adv > 0 && adv < 256) adv_freq[adv]++;
    }

    // Modal advance = cell width
    int best_adv = 6, best_count = 0;
    for (int i = 0; i < 256; i++) {
        if (adv_freq[i] > best_count) { best_count = adv_freq[i]; best_adv = i; }
    }
    glyph_advance = best_adv;

    free(data);
    return true;
}

static vlw_glyph_t *vlw_find(uint16_t codepoint) {
    // Linear scan — glyph_count is ~384, fast enough
    for (int i = 0; i < vlw_glyph_count; i++) {
        if (vlw_glyphs[i].codepoint == codepoint)
            return &vlw_glyphs[i];
    }
    return NULL;
}

static SDL_Texture *vlw_glyph_texture(vlw_glyph_t *g, SDL_Renderer *renderer) {
    if (g->texture) return g->texture;
    if (!g->bitmap || g->width == 0 || g->height == 0) return NULL;

    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, g->width, g->height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return NULL;

    SDL_LockSurface(surf);
    uint32_t *pixels = (uint32_t *)surf->pixels;
    for (int y = 0; y < g->height; y++) {
        for (int x = 0; x < g->width; x++) {
            uint8_t a = g->bitmap[y * g->width + x];
            pixels[y * surf->pitch / 4 + x] = (a << 24) | 0x00FFFFFF; // ARGB: white with alpha
        }
    }
    SDL_UnlockSurface(surf);

    g->texture = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_SetTextureBlendMode(g->texture, SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(surf);
    return g->texture;
}

// --- SDL2 state ---
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static int window_w = 640;
static int window_h = 480;

// Cell dimensions come from the VLW font
static int char_w = TEXT_MODE_CHAR_WIDTH;
static int char_h = TEXT_MODE_CHAR_HEIGHT;

static text_cell_t *grid = NULL;
static int grid_cols = TEXT_MODE_COLS;
static int grid_rows = TEXT_MODE_ROWS;
static int cursor_x = 0, cursor_y = 0;

static SDL_Color palette[16];

static void init_palette(void) {
    palette[0]  = (SDL_Color){0,0,0,255};
    palette[1]  = (SDL_Color){0,0,170,255};
    palette[2]  = (SDL_Color){0,170,0,255};
    palette[3]  = (SDL_Color){0,170,170,255};
    palette[4]  = (SDL_Color){170,0,0,255};
    palette[5]  = (SDL_Color){170,0,170,255};
    palette[6]  = (SDL_Color){170,85,0,255};
    palette[7]  = (SDL_Color){192,192,192,255};
    palette[8]  = (SDL_Color){85,85,85,255};
    palette[9]  = (SDL_Color){85,85,255,255};
    palette[10] = (SDL_Color){85,255,85,255};
    palette[11] = (SDL_Color){85,255,255,255};
    palette[12] = (SDL_Color){255,85,85,255};
    palette[13] = (SDL_Color){255,85,255,255};
    palette[14] = (SDL_Color){255,255,85,255};
    palette[15] = (SDL_Color){255,255,255,255};
}

static SDL_Color pal(uint8_t ci) {
    return ci < 16 ? palette[ci] : palette[7];
}

// --- Public API ---

bool text_mode_init(void) {
    if (window) return true; // already initialized
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }

    init_palette();
    grid = calloc(grid_cols * grid_rows, sizeof(text_cell_t));

    // Try loading the real VLW font used on hardware
    const char *vlw_paths[] = {
        "../fonts/hack-10.vlw",
        "fonts/hack-10.vlw",
        "/home/ralsina/code/esposito/fonts/hack-10.vlw",
        NULL
    };

    bool vlw_ok = false;
    for (int i = 0; vlw_paths[i]; i++) {
        if (vlw_load(vlw_paths[i])) {
            vlw_ok = true;
            fprintf(stderr, "Loaded VLW font: %s (%d glyphs, %dx%d cells)\n",
                    vlw_paths[i], vlw_glyph_count, glyph_advance, glyph_ascent + glyph_descent);
            break;
        }
    }

    if (vlw_ok) {
        char_w = glyph_advance > 0 ? glyph_advance : TEXT_MODE_CHAR_WIDTH;
        char_h = (glyph_ascent + glyph_descent) > 0 ? glyph_ascent + glyph_descent : TEXT_MODE_CHAR_HEIGHT;
    }

    window_w = grid_cols * char_w;
    window_h = grid_rows * char_h;

    // Create window after we know the size
    window = SDL_CreateWindow("Esposito Emulator",
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              window_w, window_h, SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // nearest-neighbor for crisp pixel art
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    if (!vlw_ok) {
        fprintf(stderr, "WARNING: No VLW font loaded, creating empty window\n");
    }

    return true;
}

void text_mode_clear(uint16_t bg_color) {
    for (int i = 0; i < grid_cols * grid_rows; i++) {
        grid[i].character = ' ';
        grid[i].color = 7;
        grid[i].bg_color = (uint8_t)(bg_color & 0xFF);
        grid[i].attributes = 0;
    }
}

int text_mode_get_cols(void) { return grid_cols; }
int text_mode_get_rows(void) { return grid_rows; }
int text_mode_get_char_width(void) { return char_w; }
int text_mode_get_char_height(void) { return char_h; }

void text_mode_set_cursor(int x, int y) { cursor_x = x; cursor_y = y; }
void text_mode_get_cursor(int *x, int *y) { *x = cursor_x; *y = cursor_y; }

static void put_char(int x, int y, const char *str, uint8_t color, uint8_t bg, uint8_t attr) {
    unsigned char ch = (unsigned char)str[0];
    if (x < 0 || x >= grid_cols || y < 0 || y >= grid_rows) return;
    int idx = y * grid_cols + x;
    grid[idx].character = ch;
    grid[idx].color = color;
    grid[idx].bg_color = bg;
    grid[idx].attributes = attr;
}

static void put_str(int x, int y, const char *str, uint8_t color, uint8_t bg, uint8_t attr) {
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\n') { y++; x = 0; continue; }
        if (x >= grid_cols) { x = 0; y++; }
        if (y >= grid_rows) break;
        put_char(x, y, str + i, color, bg, attr);
        x++;
    }
}

void text_mode_print_at(int x, int y, const char *str) {
    put_str(x, y, str, 7, 0, 0);
}
void text_mode_print_at_color(int x, int y, const char *str, uint16_t color) {
    put_str(x, y, str, (uint8_t)(color & 0xFF), 0, 0);
}
void text_mode_print_at_attr(int x, int y, const char *str, uint8_t color, uint8_t attr) {
    put_str(x, y, str, color, 0, attr);
}
void text_mode_print_at_attr_bg(int x, int y, const char *str, uint8_t fg, uint8_t bg, uint8_t attr) {
    put_str(x, y, str, fg, bg, attr);
}

void text_mode_printf_at(int x, int y, const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    put_str(x, y, buf, 7, 0, 0);
}
void text_mode_printf_at_color(int x, int y, uint16_t color, const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    put_str(x, y, buf, (uint8_t)(color & 0xFF), 0, 0);
}
void text_mode_printf_at_attr(int x, int y, uint8_t color, uint8_t attr, const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    put_str(x, y, buf, color, 0, attr);
}
void text_mode_printf_at_attr_bg(int x, int y, uint8_t fg, uint8_t bg, uint8_t attr, const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    put_str(x, y, buf, fg, bg, attr);
}

void text_mode_flush(void) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    for (int row = 0; row < grid_rows; row++) {
        for (int col = 0; col < grid_cols; col++) {
            int idx = row * grid_cols + col;
            text_cell_t cell = grid[idx];
            SDL_Color bg = pal(cell.bg_color);
            SDL_Color fg = pal(cell.color);

            int cx = col * char_w;
            int cy = row * char_h;

            // Background fill
            SDL_Rect bg_rect = {cx, cy, char_w, char_h};
            SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 255);
            SDL_RenderFillRect(renderer, &bg_rect);

            // Character glyph via VLW bitmap
            if (cell.character >= 32) {
                vlw_glyph_t *g = vlw_find(cell.character);
                if (g && g->width > 0 && g->height > 0) {
                    SDL_Texture *tex = vlw_glyph_texture(g, renderer);
                    if (tex) {
                        SDL_SetTextureColorMod(tex, fg.r, fg.g, fg.b);
                        SDL_Rect dst = {
                            cx + g->left_offset,
                            cy + glyph_ascent - g->top_offset,
                            g->width,
                            g->height
                        };
                        SDL_RenderCopy(renderer, tex, NULL, &dst);
                    }
                }
            }
        }
    }

    // Cursor
    SDL_Rect cur = {cursor_x * char_w, cursor_y * char_h, char_w, char_h};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 128);
    SDL_RenderFillRect(renderer, &cur);

    SDL_RenderPresent(renderer);
}

// Snapshots
text_mode_snapshot_t *text_mode_save_snapshot(void) {
    text_mode_snapshot_t *snap = malloc(sizeof(text_mode_snapshot_t));
    if (!snap) return NULL;
    snap->cols = grid_cols;
    snap->rows = grid_rows;
    snap->cursor_x = cursor_x;
    snap->cursor_y = cursor_y;
    snap->cells = malloc(grid_cols * grid_rows * sizeof(text_cell_t));
    if (snap->cells) memcpy(snap->cells, grid, grid_cols * grid_rows * sizeof(text_cell_t));
    return snap;
}
void text_mode_restore_snapshot(text_mode_snapshot_t *snap) {
    if (!snap) return;
    if (snap->cells) memcpy(grid, snap->cells, grid_cols * grid_rows * sizeof(text_cell_t));
    cursor_x = snap->cursor_x;
    cursor_y = snap->cursor_y;
}
void text_mode_free_snapshot(text_mode_snapshot_t *snap) {
    if (!snap) return;
    free(snap->cells);
    free(snap);
}
void text_mode_pixel_to_cell(int px, int py, int *cx, int *cy) {
    *cx = px / char_w;
    *cy = py / char_h;
}
void text_mode_cell_to_pixel(int cx, int cy, int *px, int *py) {
    *px = cx * char_w;
    *py = cy * char_h;
}

// Direct pixel-level text rendering for graphics mode
void text_mode_render_char(int x, int y, uint32_t codepoint, uint8_t cr, uint8_t cg, uint8_t cb) {
    vlw_glyph_t *glyph = vlw_find((uint16_t)codepoint);
    if (!glyph || glyph->width <= 0 || glyph->height <= 0) return;
    SDL_Texture *tex = vlw_glyph_texture(glyph, renderer);
    if (!tex) return;
    SDL_SetTextureColorMod(tex, cr, cg, cb);
    SDL_Rect dst = {x + glyph->left_offset, y + glyph_ascent - glyph->top_offset, glyph->width, glyph->height};
    SDL_RenderCopy(renderer, tex, NULL, &dst);
}

void text_mode_render_string(int x, int y, const char *str, uint8_t cr, uint8_t cg, uint8_t cb) {
    int cx = x;
    for (const char *p = str; *p; p++) {
        text_mode_render_char(cx, y, (uint8_t)*p, cr, cg, cb);
        cx += glyph_advance;
    }
}

SDL_Window *text_mode_get_window(void) {
    return window;
}

SDL_Renderer *text_mode_get_renderer(void) {
    return renderer;
}

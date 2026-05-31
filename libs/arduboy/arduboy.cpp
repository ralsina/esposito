#include "arduboy.h"
#include "arduboy_tunes.h"
#include <os_core.h>
#include <hardware.h>
#include <hardware_config.h>
#include <graphics_mode.h>
#include "glcdfont.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
    void *malloc(size_t size);
    void free(void *ptr);
}

static bool g_exit_requested = false;

#ifdef COLOR_BLACK
#undef COLOR_BLACK
#endif
#ifdef COLOR_WHITE
#undef COLOR_WHITE
#endif
#ifdef COLOR_GREEN
#undef COLOR_GREEN
#endif

void setup();
void loop();

extern Arduboy arduboy;

static app_context_t *g_app_ctx = NULL;
static uint32_t g_last_frame_time = 0;
static uint8_t g_pending_button_presses = 0;

#define ARDUBOY_SCREEN_WIDTH 128
#define ARDUBOY_SCREEN_HEIGHT 64
#define ARDUBOY_FONT_WIDTH 5
#define ARDUBOY_FONT_SPACING 1
#define ARDUBOY_FONT_LINE_HEIGHT 8

#define COLOR_BLACK 0
#define COLOR_WHITE 1

static uint8_t *g_arduboy_buffer = NULL;

static void set_pixel(int x, int y, bool color) {
    if (x < 0 || x >= ARDUBOY_SCREEN_WIDTH || y < 0 || y >= ARDUBOY_SCREEN_HEIGHT) return;
    if (!g_arduboy_buffer) return;
    int index = y * ARDUBOY_SCREEN_WIDTH + x;
    int byte_index = index / 2;
    uint8_t c = color ? COLOR_WHITE : COLOR_BLACK;
    if ((index & 1) == 0) {
        g_arduboy_buffer[byte_index] = (g_arduboy_buffer[byte_index] & 0x0F) | (c << 4);
    } else {
        g_arduboy_buffer[byte_index] = (g_arduboy_buffer[byte_index] & 0xF0) | c;
    }
}

static bool get_pixel(int x, int y) {
    if (x < 0 || x >= ARDUBOY_SCREEN_WIDTH || y < 0 || y >= ARDUBOY_SCREEN_HEIGHT) return false;
    if (!g_arduboy_buffer) return false;
    int index = y * ARDUBOY_SCREEN_WIDTH + x;
    int byte_index = index / 2;
    uint8_t val = g_arduboy_buffer[byte_index];
    uint8_t c = (index & 1) ? (val & 0x0F) : ((val >> 4) & 0x0F);
    return c == COLOR_WHITE;
}

static void draw_char(int x, int y, char c, int size, bool fg_color, bool bg_color) {
    unsigned char glyph = static_cast<unsigned char>(c);
    const unsigned char *bitmap = &arduboy_font[glyph * ARDUBOY_FONT_WIDTH];
    bool draw_bg = (fg_color != bg_color);

    for (int col = 0; col < ARDUBOY_FONT_WIDTH + 1; col++) {
        unsigned char column_bits = (col < ARDUBOY_FONT_WIDTH) ? bitmap[col] : 0;
        for (int row = 0; row < 8; row++) {
            bool is_fg = (column_bits & (1 << row)) != 0;
            if (is_fg) {
                for (int py = 0; py < size; py++) {
                    for (int px = 0; px < size; px++) {
                        set_pixel(x + col * size + px, y + row * size + py, fg_color);
                    }
                }
            } else if (draw_bg) {
                for (int py = 0; py < size; py++) {
                    for (int px = 0; px < size; px++) {
                        set_pixel(x + col * size + px, y + row * size + py, bg_color);
                    }
                }
            }
        }
    }
}

static int glyph_advance(char c, int size) {
    (void)c;
    return (ARDUBOY_FONT_WIDTH + ARDUBOY_FONT_SPACING) * size;
}

static int get_scale(void) {
    int dw = display_get_width();
    int dh = display_get_height();
    int sx = dw / ARDUBOY_SCREEN_WIDTH;
    int sy = dh / ARDUBOY_SCREEN_HEIGHT;
    int s = sx < sy ? sx : sy;
    return s < 1 ? 1 : s;
}

void Arduboy::begin() {
    printf("Arduboy::begin() - entering\n");

    if (!g_arduboy_buffer) {
        g_arduboy_buffer = (uint8_t *)malloc(ARDUBOY_SCREEN_WIDTH * ARDUBOY_SCREEN_HEIGHT / 2);
        if (!g_arduboy_buffer) {
            printf("Arduboy::begin() - failed to allocate arduboy buffer\n");
            return;
        }
    }

    int canvas_bytes = display_get_width() * display_get_height() / 2;
    uint8_t *screen_buf = (uint8_t *)malloc(canvas_bytes);
    if (!screen_buf) {
        printf("Arduboy::begin() - failed to allocate screen buffer\n");
        free(g_arduboy_buffer);
        g_arduboy_buffer = NULL;
        return;
    }

    graphics_mode_init(screen_buf, canvas_bytes);

    if (!graphics_mode_is_active()) {
        printf("Arduboy::begin() - graphics mode NOT active, aborting\n");
        return;
    }

    uint16_t palette[16] = {0};
    palette[0] = 0x0000;
    palette[1] = 0x07E0;
    graphics_set_palette(palette, 2);

    initialized = true;
    frameRate = 60;
    nextFrameReady = true;
    textSize = 1;
    cursorX = 0;
    cursorY = 0;
    textColor = 1;
    textBackground = 1;
    currentButtonState = 0;
    previousButtonState = 0;

    printf("Arduboy::begin() - done\n");
}

void Arduboy::clear() {
    if (g_arduboy_buffer) {
        memset(g_arduboy_buffer, 0, ARDUBOY_SCREEN_WIDTH * ARDUBOY_SCREEN_HEIGHT / 2);
    }
    graphics_clear(COLOR_BLACK);
}

void Arduboy::display() {
    if (!g_arduboy_buffer) return;
    int scale = get_scale();
    int scaled_w = ARDUBOY_SCREEN_WIDTH * scale;
    int scaled_h = ARDUBOY_SCREEN_HEIGHT * scale;
    int dst_x = (display_get_width() - scaled_w) / 2;
    int dst_y = (display_get_height() - scaled_h) / 2;
    graphics_blit_scaled(g_arduboy_buffer, ARDUBOY_SCREEN_WIDTH, ARDUBOY_SCREEN_HEIGHT, dst_x, dst_y, scale);
    graphics_flush();
}

void Arduboy::setFrameRate(uint8_t fps) {
    frameRate = fps;
    if (g_app_ctx && fps > 0) {
        uint32_t interval = 1000 / fps;
        if (interval < 16) interval = 16;
        if (interval > 1000) interval = 1000;
        g_app_ctx->timer_interval_ms = interval;
    }
}

bool Arduboy::nextFrame() {
    return true;
}

void Arduboy::setTextSize(uint8_t size) {
    textSize = size;
}

void Arduboy::setCursor(int x, int y) {
    cursorX = x;
    cursorY = y;
}

void Arduboy::print(const char *text) {
    if (!text) return;
    int char_height = ARDUBOY_FONT_LINE_HEIGHT * textSize;
    bool fg = textColor > 0;
    bool bg = textBackground > 0;
    for (size_t i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        if (c == '\n') {
            cursorX = 0;
            cursorY += char_height;
            continue;
        }
        draw_char(cursorX, cursorY, c, textSize, fg, bg);
        cursorX += glyph_advance(c, textSize);
        if (cursorX >= ARDUBOY_SCREEN_WIDTH) {
            cursorX = 0;
            cursorY += char_height;
        }
    }
}

void Arduboy::print(int val) {
    char buffer[16];
    sprintf(buffer, "%d", val);
    print(buffer);
}

void Arduboy::drawPixel(int x, int y, uint8_t color) {
    set_pixel(x, y, color > 0);
}

void Arduboy::drawRect(int x, int y, int width, int height, uint8_t color) {
    if (width <= 0 || height <= 0) return;
    bool c = color > 0;
    for (int i = 0; i < width; i++) {
        set_pixel(x + i, y, c);
        set_pixel(x + i, y + height - 1, c);
    }
    for (int i = 0; i < height; i++) {
        set_pixel(x, y + i, c);
        set_pixel(x + width - 1, y + i, c);
    }
}

void Arduboy::fillRect(int x, int y, int width, int height, uint8_t color) {
    if (width <= 0 || height <= 0) return;
    bool c = color > 0;
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            set_pixel(x + dx, y + dy, c);
        }
    }
}

void Arduboy::fillCircle(int x, int y, int radius, uint8_t color) {
    bool c = color > 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius) {
                set_pixel(x + dx, y + dy, c);
            }
        }
    }
}

bool Arduboy::pressed(uint8_t buttons) {
    return (currentButtonState & buttons) != 0;
}

bool Arduboy::notPressed(uint8_t buttons) {
    return (currentButtonState & buttons) == 0;
}

void Arduboy::initInput() {
    currentButtonState = 0;
    previousButtonState = 0;
    g_pending_button_presses = 0;
}

void Arduboy::updateInput() {
}

void Arduboy::setAppContext(app_context_t *ctx) {
    g_app_ctx = ctx;
    g_last_frame_time = 0;
}

void Arduboy::setTextColor(uint8_t color) {
    textColor = color;
}

void Arduboy::setTextBackground(uint8_t color) {
    textBackground = color;
}

void Arduboy::drawSlowXYBitmap(int x, int y, const uint8_t *bitmap, int w, int h, uint8_t color) {
    if (!bitmap) return;
    bool c = color > 0;
    int byte_width = (w + 7) / 8;
    for (int yi = 0; yi < h; yi++) {
        for (int xi = 0; xi < w; xi++) {
            if (bitmap[yi * byte_width + xi / 8] & (0x80 >> (xi & 7))) {
                set_pixel(x + xi, y + yi, c);
            }
        }
    }
}

void Arduboy::drawFastHLine(int x, int y, int w, uint8_t color) {
    bool c = color > 0;
    for (int i = 0; i < w; i++) {
        set_pixel(x + i, y, c);
    }
}

void Arduboy::drawFastVLine(int x, int y, int h, uint8_t color) {
    bool c = color > 0;
    for (int i = 0; i < h; i++) {
        set_pixel(x, y + i, c);
    }
}

void Arduboy::pollButtons() {
    previousButtonState = currentButtonState;

    // Synchronous polling: read keyboard hardware directly (like real Arduboy)
    event_t ev;
    while (keyboard_read_event(&ev)) {
        if (ev.type == EVENT_KEYBOARD) {
            // Check for OS shortcut: Ctrl+ESC (exit to launcher)
            if (ev.keyboard.pressed && ev.keyboard.key == 27 &&
                (ev.keyboard.modifiers & MODIFIER_CTRL)) {
                os_load_app("launcher");
                break;
            }

            // Map WASD/ML to Arduboy buttons
            uint8_t button = 0;
            switch (ev.keyboard.key) {
                case 'w': case 'W': button = UP_BUTTON; break;
                case 's': case 'S': button = DOWN_BUTTON; break;
                case 'a': case 'A': button = LEFT_BUTTON; break;
                case 'd': case 'D': button = RIGHT_BUTTON; break;
                case 'm': case 'M': button = A_BUTTON; break;
                case 'l': case 'L': button = B_BUTTON; break;
                default: break;
            }
            if (button != 0) {
                if (ev.keyboard.pressed) {
                    currentButtonState |= button;
                } else {
                    currentButtonState &= ~button;
                }
            }
        }
    }
}

bool Arduboy::exitRequested() {
    return g_exit_requested;
}

bool Arduboy::anyPressed(uint8_t buttons) {
    return (currentButtonState & buttons) != 0;
}

bool Arduboy::justPressed(uint8_t buttons) {
    return ((currentButtonState & buttons) != 0) && ((previousButtonState & buttons) == 0);
}

void Arduboy::initRandomSeed() {
}

void Sprites::drawOverwrite(int x, int y, const uint8_t *sprite, uint8_t frame) {
    if (!sprite) return;
    int width = sprite[0];
    int height = sprite[1];
    int bytes_per_col = (height + 7) / 8;
    const uint8_t *frame_data = sprite + 2 + frame * width * bytes_per_col;

    for (int col = 0; col < width; col++) {
        for (int byte_row = 0; byte_row < bytes_per_col; byte_row++) {
            uint8_t data = frame_data[col * bytes_per_col + byte_row];
            for (int bit = 0; bit < 8; bit++) {
                int py = byte_row * 8 + bit;
                if (py >= height) break;
                if (data & (0x80 >> bit)) {
                    set_pixel(x + col, y + py, true);
                }
            }
        }
    }
}

void arduboy_call_setup(void) {
    setup();
}

void arduboy_call_loop(void) {
    loop();
}

void arduboy_handle_key_event(char key, bool pressed) {
    uint8_t button = 0;
    switch (key) {
        case 'w': case 'W': button = UP_BUTTON; break;
        case 's': case 'S': button = DOWN_BUTTON; break;
        case 'a': case 'A': button = LEFT_BUTTON; break;
        case 'd': case 'D': button = RIGHT_BUTTON; break;
        case 'm': case 'M': button = A_BUTTON; break;
        case 'l': case 'L': button = B_BUTTON; break;
        default: return;
    }
    if (pressed) {
        arduboy.currentButtonState |= button;
    } else {
        arduboy.currentButtonState &= ~button;
    }
}

void arduboy_handle_touch_event(int x, int y, bool pressed) {
}

void arduboy_cleanup(void) {
    if (g_arduboy_buffer) {
        free(g_arduboy_buffer);
        g_arduboy_buffer = NULL;
    }
    graphics_mode_deinit();
}

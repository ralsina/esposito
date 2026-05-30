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

// Forward declare malloc/free from OS symbol table (C linkage)
extern "C" {
    void *malloc(size_t size);
    void free(void *ptr);
}

// Undefine conflicting macros from hardware_config.h
#ifdef COLOR_BLACK
#undef COLOR_BLACK
#endif
#ifdef COLOR_GREEN
#undef COLOR_GREEN
#endif

// Forward declarations for Arduino functions (these are C++ functions from the game)
void setup();
void loop();

// Global instances are defined in the Numbers game, not here
extern Arduboy arduboy;

// Global state for hardware integration
static app_context_t *g_app_ctx = NULL;
static uint32_t g_last_frame_time = 0;
static uint8_t g_pending_button_presses = 0;

// Arduboy screen: 128x64 pixels, 1-bit color
#define ARDUBOY_SCREEN_WIDTH 128
#define ARDUBOY_SCREEN_HEIGHT 64
#define ARDUBOY_FONT_WIDTH 5
#define ARDUBOY_FONT_SPACING 1
#define ARDUBOY_FONT_LINE_HEIGHT 8

// 4bpp sprite buffer allocated from app heap (320x240/2 = 38400 bytes)
static uint8_t *g_sprite_buffer = NULL;

// Palette indices for Arduboy's 1-bit colors
#define COLOR_BLACK 0
#define COLOR_GREEN 1

// Helper: set pixel in 4bpp sprite buffer via graphics_mode
static void set_pixel(int x, int y, bool color) {
    if (x < 0 || x >= ARDUBOY_SCREEN_WIDTH || y < 0 || y >= ARDUBOY_SCREEN_HEIGHT) return;

    int offset_x = (display_get_width() - ARDUBOY_SCREEN_WIDTH) / 2;
    int offset_y = (display_get_height() - ARDUBOY_SCREEN_HEIGHT) / 2;

    graphics_draw_pixel(offset_x + x, offset_y + y, color ? COLOR_GREEN : COLOR_BLACK);
}

// Helper: get pixel from sprite buffer (read back from sprite)
static bool get_pixel(int x, int y) {
    if (x < 0 || x >= ARDUBOY_SCREEN_WIDTH || y < 0 || y >= ARDUBOY_SCREEN_HEIGHT) return false;

    int offset_x = (display_get_width() - ARDUBOY_SCREEN_WIDTH) / 2;
    int offset_y = (display_get_height() - ARDUBOY_SCREEN_HEIGHT) / 2;

    if (!g_sprite_buffer) return false;
    int screen_x = offset_x + x;
    int screen_y = offset_y + y;
    int index = (screen_y * display_get_width() + screen_x) / 2;
    bool is_low = (screen_x % 2) == 0;
    uint8_t color = is_low ? (g_sprite_buffer[index] >> 4) & 0x0F : g_sprite_buffer[index] & 0x0F;
    return color == COLOR_GREEN;
}

// Helper: draw character to framebuffer using the classic Arduboy 5x7 font.
static void draw_char(int x, int y, char c, int size) {
    unsigned char glyph = static_cast<unsigned char>(c);
    const unsigned char *bitmap = &arduboy_font[glyph * ARDUBOY_FONT_WIDTH];

    for (int col = 0; col < ARDUBOY_FONT_WIDTH; col++) {
        unsigned char column_bits = bitmap[col];
        for (int row = 0; row < 8; row++) {
            if ((column_bits & (1 << row)) == 0) {
                continue;
            }

            for (int py = 0; py < size; py++) {
                for (int px = 0; px < size; px++) {
                    set_pixel(x + col * size + px,
                             y + row * size + py,
                             true);
                }
            }
        }
    }
}

static int glyph_advance(char c, int size) {
    (void)c;
    return (ARDUBOY_FONT_WIDTH + ARDUBOY_FONT_SPACING) * size;
}

// Helper: display the sprite buffer
static void flush_framebuffer() {
    graphics_flush();
}

void Arduboy::begin() {
    printf("Arduboy::begin() - entering\n");

    // Allocate sprite buffer from app heap
    if (!g_sprite_buffer) {
        int buf_size = display_get_width() * display_get_height() / 2;
        g_sprite_buffer = (uint8_t *)malloc(buf_size);
        if (!g_sprite_buffer) {
            printf("Arduboy::begin() - failed to allocate sprite buffer\n");
            return;
        }
        printf("Arduboy::begin() - sprite buffer allocated at %p\n", g_sprite_buffer);
    }

    // Initialize graphics mode with our pre-allocated buffer
    graphics_mode_init(g_sprite_buffer, display_get_width() * display_get_height() / 2);
    printf("Arduboy::begin() - graphics_mode_init returned, active=%d\n", graphics_mode_is_active());

    if (!graphics_mode_is_active()) {
        printf("Arduboy::begin() - graphics mode NOT active, aborting\n");
        return;
    }

    // Set palette: index 0 = black, index 1 = green (Arduboy colors)
    uint16_t palette[16] = {0};
    palette[0] = 0x0000; // Black
    palette[1] = 0x07E0; // Green
    graphics_set_palette(palette, 2);

    initialized = true;
    frameRate = 60;
    nextFrameReady = true;
    textSize = 1;
    cursorX = 0;
    cursorY = 0;
    currentButtonState = 0;
    previousButtonState = 0;

    printf("Arduboy::begin() - done\n");
}

void Arduboy::clear() {
    graphics_clear(COLOR_BLACK);
}

void Arduboy::display() {
    flush_framebuffer();
}

void Arduboy::setFrameRate(uint8_t fps) {
    frameRate = fps;
    // Update timer interval in app context (minimum 16ms for 60fps, maximum 100ms for 10fps)
    if (g_app_ctx && fps > 0) {
        uint32_t interval = 1000 / fps;
        if (interval < 16) interval = 16;      // Cap at ~60fps
        if (interval > 1000) interval = 1000;  // Cap at 1fps
        g_app_ctx->timer_interval_ms = interval;
    }
}

bool Arduboy::nextFrame() {
    // Simple frame limiter - always return true for now
    // The timer system handles the actual frame rate
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

    // Print character by character
    int char_height = ARDUBOY_FONT_LINE_HEIGHT * textSize;

    for (size_t i = 0; text[i] != '\0'; i++) {
        char c = text[i];

        if (c == '\n') {
            cursorX = 0;
            cursorY += char_height;
            continue;
        }

        draw_char(cursorX, cursorY, c, textSize);
        cursorX += glyph_advance(c, textSize);

        // Wrap to next line if needed
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

    int offset_x = (display_get_width() - ARDUBOY_SCREEN_WIDTH) / 2;
    int offset_y = (display_get_height() - ARDUBOY_SCREEN_HEIGHT) / 2;
    uint8_t c = color > 0 ? COLOR_GREEN : COLOR_BLACK;

    graphics_draw_rect(offset_x + x, offset_y + y, width, height, c);
}

void Arduboy::fillRect(int x, int y, int width, int height, uint8_t color) {
    if (width <= 0 || height <= 0) return;

    int offset_x = (display_get_width() - ARDUBOY_SCREEN_WIDTH) / 2;
    int offset_y = (display_get_height() - ARDUBOY_SCREEN_HEIGHT) / 2;
    uint8_t c = color > 0 ? COLOR_GREEN : COLOR_BLACK;

    graphics_fill_rect(offset_x + x, offset_y + y, width, height, c);
}

void Arduboy::fillCircle(int x, int y, int radius, uint8_t color) {
    int offset_x = (display_get_width() - ARDUBOY_SCREEN_WIDTH) / 2;
    int offset_y = (display_get_height() - ARDUBOY_SCREEN_HEIGHT) / 2;
    uint8_t c = color > 0 ? COLOR_GREEN : COLOR_BLACK;

    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx*dx + dy*dy <= radius*radius) {
                set_pixel(x + dx, y + dy, color > 0);
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
    previousButtonState = currentButtonState;
    currentButtonState = g_pending_button_presses;
    g_pending_button_presses = 0;
}

void Arduboy::setAppContext(app_context_t *ctx) {
    g_app_ctx = ctx;
    g_last_frame_time = 0;  // Will be set by first frame
}

// Helper functions
void arduboy_call_setup(void) {
    printf("Calling setup()\n");
    setup();
}

void arduboy_call_loop(void) {
    loop();
}

void arduboy_handle_key_event(char key, bool pressed) {
    uint8_t button = 0;

    switch (key) {
        case 'w': case 'W':
            button = UP_BUTTON;
            break;
        case 's': case 'S':
            button = DOWN_BUTTON;
            break;
        case 'a': case 'A':
            button = LEFT_BUTTON;
            break;
        case 'd': case 'D':
            button = RIGHT_BUTTON;
            break;
        case 'm': case 'M':
            button = A_BUTTON;
            break;
        case 'l': case 'L':
            button = B_BUTTON;
            break;
        default:
            return;
    }

    if (pressed) {
        g_pending_button_presses |= button;
    }
}

void arduboy_handle_touch_event(int x, int y, bool pressed) {
    // Map touch zones to buttons
    // Left side: LEFT, Right side: RIGHT, etc.
}

void arduboy_cleanup(void) {
    if (g_sprite_buffer) {
        free(g_sprite_buffer);
        g_sprite_buffer = NULL;
    }
    graphics_mode_deinit();
}

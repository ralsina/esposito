#include "arduboy.h"
#include "arduboy_tunes.h"
#include <os_core.h>
#include <hardware.h>
#include <hardware_config.h>
#include "glcdfont.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for Arduino functions (these are C++ functions from the game)
void setup();
void loop();

// Global instances are defined in the Numbers game, not here
// Arduboy arduboy;
// ArduboyTunes tunes;
extern Arduboy arduboy;

// Global state for hardware integration
static app_context_t *g_app_ctx = NULL;
static uint32_t g_last_frame_time = 0;
static uint8_t g_pending_button_presses = 0;

// Arduboy screen: 128x64 pixels, 1-bit color
#define ARDUBOY_SCREEN_WIDTH 128
#define ARDUBOY_SCREEN_HEIGHT 64
#define ARDUBOY_BUFFER_SIZE ((ARDUBOY_SCREEN_WIDTH * ARDUBOY_SCREEN_HEIGHT) / 8)
#define ARDUBOY_FONT_WIDTH 5
#define ARDUBOY_FONT_SPACING 1
#define ARDUBOY_FONT_LINE_HEIGHT 8

// 1-bit framebuffer for Arduboy screen (row-major, MSB first)
static uint8_t arduboy_framebuffer[ARDUBOY_BUFFER_SIZE];

// Helper: set pixel in 1-bit framebuffer
static void set_pixel(int x, int y, bool color) {
    if (x < 0 || x >= ARDUBOY_SCREEN_WIDTH || y < 0 || y >= ARDUBOY_SCREEN_HEIGHT) return;

    int index = (y * ARDUBOY_SCREEN_WIDTH + x) / 8;
    int bit = 7 - ((y * ARDUBOY_SCREEN_WIDTH + x) % 8);

    if (color) {
        arduboy_framebuffer[index] |= (1 << bit);
    } else {
        arduboy_framebuffer[index] &= ~(1 << bit);
    }
}

// Helper: get pixel from 1-bit framebuffer
static bool get_pixel(int x, int y) {
    if (x < 0 || x >= ARDUBOY_SCREEN_WIDTH || y < 0 || y >= ARDUBOY_SCREEN_HEIGHT) return false;

    int index = (y * ARDUBOY_SCREEN_WIDTH + x) / 8;
    int bit = 7 - ((y * ARDUBOY_SCREEN_WIDTH + x) % 8);

    return (arduboy_framebuffer[index] >> bit) & 1;
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

// Helper: convert framebuffer to RGB565 and display
static void flush_framebuffer() {
    int offset_x = (SCREEN_WIDTH - ARDUBOY_SCREEN_WIDTH) / 2;
    int offset_y = (SCREEN_HEIGHT - ARDUBOY_SCREEN_HEIGHT) / 2;

    for (int y = 0; y < ARDUBOY_SCREEN_HEIGHT; y++) {
        for (int x = 0; x < ARDUBOY_SCREEN_WIDTH; x++) {
            bool pixel = get_pixel(x, y);
            uint16_t color = pixel ? COLOR_GREEN : COLOR_BLACK;
            display_draw_pixel(offset_x + x, offset_y + y, color);
        }
    }
}

void Arduboy::begin() {
    printf("Arduboy::begin() - initializing\n");

    // Clear framebuffer
    memset(arduboy_framebuffer, 0, ARDUBOY_BUFFER_SIZE);

    // Clear the actual display
    display_clear(0x0000);

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
    // Clear framebuffer to black
    memset(arduboy_framebuffer, 0, ARDUBOY_BUFFER_SIZE);
}

void Arduboy::display() {
    // Convert and display framebuffer
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
    if (width <= 0 || height <= 0) {
        return;
    }

    for (int px = x; px < x + width; px++) {
        set_pixel(px, y, color > 0);
        set_pixel(px, y + height - 1, color > 0);
    }

    for (int py = y; py < y + height; py++) {
        set_pixel(x, py, color > 0);
        set_pixel(x + width - 1, py, color > 0);
    }
}

void Arduboy::fillRect(int x, int y, int width, int height, uint8_t color) {
    if (width <= 0 || height <= 0) {
        return;
    }

    for (int py = y; py < y + height; py++) {
        for (int px = x; px < x + width; px++) {
            set_pixel(px, py, color > 0);
        }
    }
}

void Arduboy::fillCircle(int x, int y, int radius, uint8_t color) {
    // Simple circle drawing algorithm
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
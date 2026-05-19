// Touch Debug App
// Displays raw touch values and converted pixel coordinates for debugging

#include "os_core.h"
#include "hardware.h"
#include "text_mode.h"
#include <stdio.h>
#include <string.h>

static app_context_t *ctx = NULL;

static void show_debug_screen(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    text_mode_clear(TEXT_COLOR_BLACK);

    // Title
    char title[64];
    snprintf(title, sizeof(title), "Touch Debug - Rotation %d (%dx%d)",
             display_get_rotation(), display_get_width(), display_get_height());
    int title_len = strlen(title);
    int title_x = (cols - title_len) / 2;
    text_mode_print_at_attr(title_x, 0, title, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);

    text_mode_print_at_color(0, 2, "Touch screen to see raw and converted coordinates:", TEXT_COLOR_WHITE);
    text_mode_print_at_color(0, 3, "[R] Next rotation  [Q] Quit", TEXT_COLOR_CYAN);

    text_mode_flush();
}

static void display_touch_info(uint16_t screen_x, uint16_t screen_y) {
    // Clear previous touch data area
    for (int i = 5; i < 16; i++) {
        text_mode_print_at_color(0, i, "                                          ", TEXT_COLOR_BLACK);
    }

    char line[64];
    int row = 5;

    // Show calibration constants
    snprintf(line, sizeof(line), "Calib: x_min=210, x_range=3600, y_min=260, y_range=3600");
    text_mode_print_at_color(0, row, line, TEXT_COLOR_BRIGHT_MAGENTA);
    row++;

    // Screen dimensions
    snprintf(line, sizeof(line), "Screen: %dx%d", display_get_width(), display_get_height());
    text_mode_print_at_color(0, row, line, TEXT_COLOR_CYAN);
    row++;

    // Rotation
    int rotation = display_get_rotation();
    const char *rot_names[] = {"0° (Portrait)", "90° (Landscape)", "180° (Inverted Portrait)", "270° (Inverted Landscape)"};
    snprintf(line, sizeof(line), "Rotation: %d (%s)", rotation, rot_names[rotation]);
    text_mode_print_at_color(0, row, line, TEXT_COLOR_CYAN);
    row++;

    row++; // Empty line

    // Converted coordinates
    snprintf(line, sizeof(line), "Screen X: %4d", screen_x);
    text_mode_print_at_color(0, row, line, TEXT_COLOR_BRIGHT_GREEN);
    row++;

    snprintf(line, sizeof(line), "Screen Y: %4d", screen_y);
    text_mode_print_at_color(0, row, line, TEXT_COLOR_BRIGHT_GREEN);
    row++;

    row++; // Empty line

    // Expected ranges
    int max_x = display_get_width() - 1;
    int max_y = display_get_height() - 1;
    snprintf(line, sizeof(line), "Expected: X: 0-%d, Y: 0-%d", max_x, max_y);
    text_mode_print_at_color(0, row, line, TEXT_COLOR_WHITE);
    row++;

    // Character conversion
    int char_width = text_mode_get_char_width();
    int char_height = text_mode_get_char_height();
    int char_x = screen_x / char_width;
    int char_y = screen_y / char_height;

    snprintf(line, sizeof(line), "Char: %dx%d pixels -> col %d, row %d", char_width, char_height, char_x, char_y);
    text_mode_print_at_color(0, row, line, TEXT_COLOR_YELLOW);
    row++;

    text_mode_flush();
}

int app_init(app_context_t *context) {
    ctx = context;
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH;
    show_debug_screen();
    return 0;
}

int app_event(app_context_t *context, event_t *event) {
    (void)context;

    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;

        if (key == 'q' || key == 'Q') {
            return -1; // Exit app
        }

        if (key == 'r' || key == 'R') {
            int current = display_get_rotation();
            display_set_rotation((current + 1) % 4);
            show_debug_screen();
        }
    }

    if (event->type == EVENT_TOUCH && event->touch.pressed) {
        display_touch_info(event->touch.x, event->touch.y);
    }

    return 0;
}

int app_checkpoint(app_context_t *context) {
    (void)context;
    return 0;
}

int app_close(app_context_t *context) {
    (void)context;
    return 0;
}

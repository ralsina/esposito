#include "os_core.h"
#include "text_mode.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "font_test";
static font_id_t current_font = FONT_SPLEEN_5X8;

static void draw_screen(void) {
    text_mode_clear(TEXT_COLOR_BLACK);

    text_mode_print_at_attr(0, 0, "Font Test", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);

    char buf[64];
    snprintf(buf, sizeof(buf), "Font: %s", font_table[current_font].name);
    text_mode_print_at_attr(0, 1, buf, TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);

    snprintf(buf, sizeof(buf), "Grid: %dx%d cells (%dx%d px)",
             text_mode_get_cols(), text_mode_get_rows(),
             text_mode_get_char_width(), text_mode_get_char_height());
    text_mode_print_at_attr(0, 2, buf, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    text_mode_printf_at_attr(0, 4, TEXT_COLOR_GREEN, TEXT_ATTR_NORMAL,
                             "max col: %d  max row: %d", cols - 1, rows - 1);

    text_mode_print_at_attr(0, 6, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(0, 7, "abcdefghijklmnopqrstuvwxyz", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(0, 8, "0123456789 !@#$%%^&*()_+-=", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr(0, 10, "16-color palette:", TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);
    for (int i = 0; i < 16; i++) {
        snprintf(buf, sizeof(buf), "%02d", i);
        int x = (i % 8) * (text_mode_get_char_width() * 4);
        int y = 11 + (i / 8);
        if (x + 3 * text_mode_get_char_width() < cols * text_mode_get_char_width()) {
            text_mode_print_at_attr(x / text_mode_get_char_width(), y, buf, i, TEXT_ATTR_NORMAL);
        }
    }

    int attr_y = 13;
    text_mode_print_at_attr(0, attr_y, "Normal   Bold  Underline  Inverse", TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(0, attr_y + 1, "Normal", TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(7, attr_y + 1, "Bold", TEXT_COLOR_GREEN, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(14, attr_y + 1, "Underline", TEXT_COLOR_CYAN, TEXT_ATTR_UNDERLINE);
    text_mode_print_at_attr(24, attr_y + 1, "Inverse", TEXT_COLOR_RED, TEXT_ATTR_INVERSE);

    int bg_y = attr_y + 3;
    text_mode_print_at_attr(0, bg_y, "BG colors:", TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);
    text_mode_print_at_attr_bg(0, bg_y + 1, "  Black  ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(9, bg_y + 1, "  Blue   ", TEXT_COLOR_WHITE, TEXT_COLOR_BLUE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(18, bg_y + 1, "  Green  ", TEXT_COLOR_BLACK, TEXT_COLOR_GREEN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(27, bg_y + 1, "  Red    ", TEXT_COLOR_WHITE, TEXT_COLOR_RED, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(0, bg_y + 2, "  Cyan   ", TEXT_COLOR_BLACK, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(9, bg_y + 2, " Magenta ", TEXT_COLOR_WHITE, TEXT_COLOR_MAGENTA, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(18, bg_y + 2, "  Yellow ", TEXT_COLOR_BLACK, TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr_bg(27, bg_y + 2, "  White  ", TEXT_COLOR_BLACK, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr(0, rows - 3, "SPACE: toggle font  CTRL+ESC: launcher", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
}

void app_init(app_context_t *ctx) {
    current_font = (font_id_t)checkpoint_load_int("font");
    if ((int)current_font < 0 || current_font >= FONT_COUNT) current_font = FONT_SPLEEN_5X8;

    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    text_mode_init_ex(current_font);
    draw_screen();

    os_log(TAG, "font_test started with %s", font_table[current_font].name);
}

void app_checkpoint(app_context_t *ctx) {
    checkpoint_save_int("font", current_font);
}

void app_close(app_context_t *ctx) {
    text_mode_clear(TEXT_COLOR_BLACK);
    os_log(TAG, "font_test closing");
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        if (event->keyboard.key == ' ') {
            current_font = (current_font + 1) % FONT_COUNT;
            text_mode_init_ex(current_font);
            draw_screen();
            os_log(TAG, "switched to %s", font_table[current_font].name);
        }
    }
}

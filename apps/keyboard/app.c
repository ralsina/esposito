#include "os_core.h"
#include "text_mode.h"
#include "ui.h"
#include "ui_button.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_MAX 64
#define MAX_BUTTONS 60

static const char *TAG = "keyboard";

static char input_buffer[INPUT_MAX] = {0};
static int cursor_pos = 0;

// Keyboard layout: 5 rows, each with up to 14 keys (including NULL terminators)
// Row 0: numbers and symbols
// Row 1: qwerty row
// Row 2: asdfgh row
// Row 3: zxcvb row
// Row 4: space, backspace, enter
static const char *keyboard_layout[5][14] = {
    {"1","2","3","4","5","6","7","8","9","0","-","=", NULL, NULL},
    {"q","w","e","r","t","y","u","i","o","p","[","]","\\", NULL},
    {"a","s","d","f","g","h","j","k","l",";","'", NULL, NULL, NULL},
    {"z","x","c","v","b","n","m",",",".","/", NULL, NULL, NULL, NULL},
    {"SPACE","BKSP","ENTER", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}
};

static ui_button_t *buttons[MAX_BUTTONS];
static const char *button_labels[MAX_BUTTONS];
static int button_count = 0;

static bool needs_redraw = true;

// Forward declarations
static void create_keyboard(void);
static void destroy_keyboard(void);
static void update_input_display(void);
static void process_key_press(const char *key_label);
static void render(void);

void app_init(app_context_t *ctx) {
    os_log(TAG, "Keyboard app initializing");

    if (!text_mode_init()) {
        os_log(TAG, "Failed to init text mode");
        return;
    }

    // Clear input buffer
    memset(input_buffer, 0, sizeof(input_buffer));
    cursor_pos = 0;

    // Subscribe to touch and keyboard events (optional)
    ctx->subscriptions = EVENT_TOUCH | EVENT_KEYBOARD;
    ctx->timer_interval_ms = 500; // for cursor blinking, but we'll use steady cursor for now

    // Create keyboard buttons
    create_keyboard();

    // Initial render
    needs_redraw = true;
    render();

    os_log(TAG, "Keyboard app initialized");
}

void app_checkpoint(app_context_t *ctx) {
    // No state to save for now
}

void app_close(app_context_t *ctx) {
    os_log(TAG, "Keyboard app cleanup");

    destroy_keyboard();

    text_mode_clear(TEXT_COLOR_BLACK);
}

static void create_keyboard(void) {
    int screen_cols = text_mode_get_cols();
    int screen_rows = text_mode_get_rows();

    // We'll use the first row for input and cursor, leave second row blank, keyboard starts at row 2
    int input_y = 0;
    int keyboard_start_y = 2; // row index where keyboard starts

    // Available dimensions for keyboard
    int available_cols = screen_cols;
    int available_rows = screen_rows - keyboard_start_y;

    // We want 5 rows of keys
    int keyboard_rows = 5;
    int vertical_gap = 1; // gap between rows in character units
    int horizontal_gap = 1; // gap between keys in a row

    int button_height;
    int key_width;

    // Calculate button height: distribute available rows among keyboard rows and gaps
    int total_vertical_gap = vertical_gap * (keyboard_rows - 1);
    if (available_rows <= total_vertical_gap) {
        // Not enough space, fallback
        button_height = 1;
        vertical_gap = 0;
    } else {
        button_height = (available_rows - total_vertical_gap) / keyboard_rows;
        if (button_height < 1) {
            button_height = 1;
        }
    }

    // We'll create buttons row by row
    button_count = 0;
    for (int row = 0; row < keyboard_rows; row++) {
        // Count number of keys in this row (until NULL)
        int num_keys = 0;
        while (num_keys < 14 && keyboard_layout[row][num_keys] != NULL) {
            num_keys++;
        }
        if (num_keys == 0) continue;

        // Calculate usable width and key width for this row
        int total_horizontal_gap = horizontal_gap * (num_keys - 1);
        if (available_cols <= total_horizontal_gap) {
            // Not enough space, fallback
            key_width = 1;
            horizontal_gap = 0;
        } else {
            key_width = (available_cols - total_horizontal_gap) / num_keys;
            if (key_width < 1) key_width = 1;
        }

        int start_x = 0; // left margin 0 for now, we can adjust if needed
        int y = keyboard_start_y + row * (button_height + vertical_gap);

        for (int col = 0; col < num_keys; col++) {
            const char *label = keyboard_layout[row][col];
            int x = start_x;
            int width = key_width;

            // Create button
            ui_button_t *btn = ui_button_create(x, y, width, button_height, label);
            if (btn) {
                ui_button_set_colors(btn, TEXT_COLOR_WHITE, TEXT_COLOR_BLUE);
                // We'll store the label for later use
                if (button_count < MAX_BUTTONS) {
                    buttons[button_count] = btn;
                    button_labels[button_count] = label;
                    button_count++;
                } else {
                    ui_button_destroy(btn);
                }
            }

            start_x += width + horizontal_gap;
        }
    }
}

static void destroy_keyboard(void) {
    for (int i = 0; i < button_count; i++) {
        if (buttons[i]) {
            ui_button_destroy(buttons[i]);
            buttons[i] = NULL;
        }
    }
    button_count = 0;
}

static void process_key_press(const char *key_label) {
    if (strcmp(key_label, "SPACE") == 0) {
        if (cursor_pos < INPUT_MAX - 1) {
            input_buffer[cursor_pos++] = ' ';
            input_buffer[cursor_pos] = '\0';
        }
    } else if (strcmp(key_label, "BKSP") == 0) {
        if (cursor_pos > 0) {
            input_buffer[--cursor_pos] = '\0';
        }
    } else if (strcmp(key_label, "ENTER") == 0) {
        // Store the result in config for the caller to retrieve
        os_settings_set_string("helper/text/result", input_buffer);
        // Clear input for next entry
        memset(input_buffer, 0, sizeof(input_buffer));
        cursor_pos = 0;
    } else {
        // Regular character: we expect a single character
        if (strlen(key_label) == 1 && cursor_pos < INPUT_MAX - 1) {
            input_buffer[cursor_pos++] = key_label[0];
            input_buffer[cursor_pos] = '\0';
        }
    }
}

static void render(void) {
    if (!needs_redraw) {
        return;
    }

    // Clear screen
    text_mode_clear(TEXT_COLOR_BLACK);

    // Draw input buffer and cursor
    // We'll draw the input buffer at (0,0)
    text_mode_print_at(0, 0, input_buffer);
    // Draw cursor as an underscore at the current cursor position
    // But note: if the cursor is at the end of the string, we want to show it after the last character.
    // We'll draw the cursor at (cursor_pos, 0) as '_'
    if (cursor_pos < INPUT_MAX) {
        // We'll draw the cursor only if it's within the buffer bounds (it always is)
        text_mode_print_at(cursor_pos, 0, "_");
    }

    // Draw all buttons
    for (int i = 0; i < button_count; i++) {
        if (buttons[i]) {
            ui_button_draw(buttons[i]);
        }
    }

    text_mode_flush();
    needs_redraw = false;
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_TOUCH && event->touch.pressed) {
        // Convert pixel coordinates to character coordinates
        int char_width = text_mode_get_char_width();
        int char_height = text_mode_get_char_height();
        int touch_col = event->touch.x / char_width;
        int touch_row = event->touch.y / char_height;

        // Check each button
        for (int i = 0; i < button_count; i++) {
            ui_button_t *btn = buttons[i];
            if (!btn) continue;

            // Check if touch is within button bounds
            if (touch_col >= btn->x && touch_col < btn->x + btn->width &&
                touch_row >= btn->y && touch_row < btn->y + btn->height) {
                // Button pressed
                const char *label = button_labels[i];
                process_key_press(label);
                needs_redraw = true;
                render(); // render immediately for feedback
                return;
            }
        }
    } else if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        // Optional: handle physical keyboard input
        char key = event->keyboard.key;
        // We'll only handle printable ASCII for simplicity
        if (key >= 32 && key <= 126) {
            if (cursor_pos < INPUT_MAX - 1) {
                input_buffer[cursor_pos++] = key;
                input_buffer[cursor_pos] = '\0';
                needs_redraw = true;
                render();
            }
        } else if (key == 8 || key == 127) { // Backspace or Delete
            if (cursor_pos > 0) {
                input_buffer[--cursor_pos] = '\0';
                needs_redraw = true;
                render();
            }
        } else if (key == '\r' || key == '\n') { // Enter
            // Store result and clear input
            os_settings_set_string("helper/text/result", input_buffer);
            memset(input_buffer, 0, sizeof(input_buffer));
            cursor_pos = 0;
            needs_redraw = true;
            render();
        }
    } else if (event->type == EVENT_TIMER) {
        // We could blink the cursor here, but we are using steady cursor for now.
        // If we wanted to blink, we would toggle a flag and set needs_redraw.
        // For now, we do nothing.
    }
}

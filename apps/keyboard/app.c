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

    int screen_cols = text_mode_get_cols();
    int screen_rows = text_mode_get_rows();

    // Clear screen
    text_mode_clear(TEXT_COLOR_BLACK);

    // Input box: 2 rows high, full width, starting at row 0
    int box_top = 0;
    int box_left = 0;
    int box_width = screen_cols;
    int box_height = 2;

    // Draw top border with proper corners
    // Top-left corner: top + left borders
    text_mode_print_at_attr_bg(box_left, box_top, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_BORDER_TOP | TEXT_ATTR_BORDER_LEFT);
    // Top border (excluding corners)
    for (int x = box_left + 1; x < box_left + box_width - 1; x++) {
        text_mode_print_at_attr_bg(x, box_top, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_BORDER_TOP);
    }
    // Top-right corner: top + right borders
    text_mode_print_at_attr_bg(box_left + box_width - 1, box_top, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_BORDER_TOP | TEXT_ATTR_BORDER_RIGHT);

    // Draw bottom border with proper corners
    // Bottom-left corner: bottom + left borders
    text_mode_print_at_attr_bg(box_left, box_top + box_height - 1, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_UNDERLINE | TEXT_ATTR_BORDER_LEFT);
    // Bottom border (excluding corners)
    for (int x = box_left + 1; x < box_left + box_width - 1; x++) {
        text_mode_print_at_attr_bg(x, box_top + box_height - 1, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_UNDERLINE);
    }
    // Bottom-right corner: bottom + right borders
    text_mode_print_at_attr_bg(box_left + box_width - 1, box_top + box_height - 1, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_UNDERLINE | TEXT_ATTR_BORDER_RIGHT);

    // Draw input text in the inner area (with padding)
    int inner_top = box_top;
    int inner_left = box_left + 1;
    int inner_width = box_width - 2; // because we have left and right borders (1 char each)

    // Determine what to show: always input_buffer (no prompt concept in this version)
    const char *text_to_show = input_buffer;
    int text_len = strlen(text_to_show);
    if (text_len > inner_width) text_len = inner_width;

    // Choose color: white for input
    uint16_t text_color = TEXT_COLOR_WHITE;

    // Draw the text
    for (int i = 0; i < text_len; i++) {
        char c = text_to_show[i];
        char str[2] = {c, '\0'};
        text_mode_print_at_attr_bg(inner_left + i, inner_top, str, text_color, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
    }

    // Draw the cursor: only if cursor is within inner width
    if (cursor_pos < inner_width) {
        // Draw the cursor as the character at that position with underscore to indicate cursor position
        if (cursor_pos < text_len) {
            char c = text_to_show[cursor_pos];
            char str[2] = {c, '\0'};
            text_mode_print_at_attr_bg(inner_left + cursor_pos, inner_top, str, text_color, TEXT_COLOR_BLACK, TEXT_ATTR_UNDERLINE);
        } else {
            // Cursor is past the end of text, show underscore at cursor position
            text_mode_print_at_attr_bg(inner_left + cursor_pos, inner_top, "_", text_color, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
        }
    }
    // Note: if cursor_pos >= inner_width, we do not draw the cursor specially (it would be at or past the border)

    // Draw all buttons (starting from row 2 to leave space for input box)
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

        // Check if touch is in the input area (row 0, columns 1 to screen_cols-2)
        int screen_cols = text_mode_get_cols();
        int input_area_top = 0; // box_top
        int input_area_left = 1; // box_left + 1 (for left border)
        int input_area_width = screen_cols - 2; // box_width - 2 (for left and right borders)
        
        if (touch_row == input_area_top && 
            touch_col >= input_area_left && 
            touch_col < input_area_left + input_area_width) {
            // Set cursor position based on touch position within input area
            // Subtract 1 to account for the left border/padding
            cursor_pos = touch_col - input_area_left;
            // Ensure cursor_pos doesn't exceed input_buffer length
            int input_len = strlen(input_buffer);
            if (cursor_pos > input_len) {
                cursor_pos = input_len;
            }
            needs_redraw = true;
            render();
            return;
        }

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
                // Shift characters right to make space for the new character
                int len = strlen(input_buffer);
                for (int i = len; i >= cursor_pos; i--) {
                    input_buffer[i + 1] = input_buffer[i];
                }
                input_buffer[cursor_pos++] = key;
                input_buffer[cursor_pos] = '\0';
                needs_redraw = true;
                render();
            }
        } else if (key == 8 || key == 127) { // Backspace or Delete
            if (cursor_pos > 0) {
                // Shift characters left to fill the gap
                for (int i = cursor_pos; i < INPUT_MAX; i++) {
                    input_buffer[i - 1] = input_buffer[i];
                }
                cursor_pos--;
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

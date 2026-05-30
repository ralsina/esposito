/*
 * Calculator App with UI Library
 * 4-function calculator with proper button layout
 */

#include "os_core.h"
#include "text_mode.h"
#include "ui.h"
#include "ui_toolbar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Declare external functions
extern float strtof(const char *str, char **endptr);

#define MAX_DISPLAY 20
#define NUM_TOOLBARS 5

static char display_buffer[MAX_DISPLAY] = "0";
static int display_pos = 1;
static float current_value = 0.0f;
static float stored_value = 0.0f;
static char pending_op = 0;
static bool new_entry = true;
static bool decimal_entered = false;

static ui_toolbar_t *toolbars[NUM_TOOLBARS];

// Button layout: 4 columns x 5 rows
// Standard calculator layout
static const char *toolbar_labels[NUM_TOOLBARS][4] = {
    {"C", "\u00B1", "%", "/"},
    {"7", "8", "9", "*"},
    {"4", "5", "6", "-"},
    {"1", "2", "3", "+"},
    {"0", ".", "=", "\u2718"}
};

// Forward declarations
static void redraw_all(void);
void draw_display(void);

// Button callback functions
void button_digit(ui_button_t *button, void *user_data) {
    char digit = *((char*)user_data);

    if (new_entry) {
        strcpy(display_buffer, "0");
        display_pos = 0;
        new_entry = false;
    }

    if (display_pos < MAX_DISPLAY - 1) {
        if (strcmp(display_buffer, "0") == 0 && digit != '0') {
            display_pos = 0;
            display_buffer[display_pos++] = digit;
            display_buffer[display_pos] = '\0';
        } else if (strcmp(display_buffer, "0") != 0 || digit != '0') {
            display_buffer[display_pos++] = digit;
            display_buffer[display_pos] = '\0';
        }
    }

    redraw_all();
}

void button_decimal(ui_button_t *button, void *user_data) {
    if (!decimal_entered && display_pos < MAX_DISPLAY - 1) {
        if (new_entry) {
            strcpy(display_buffer, "0");
            display_pos = 1;
            new_entry = false;
        }
        display_buffer[display_pos++] = '.';
        display_buffer[display_pos] = '\0';
        decimal_entered = true;
    }

    redraw_all();
}

void button_operator(ui_button_t *button, void *user_data) {
    char op = *((char*)user_data);
    float display_val = strtof(display_buffer, NULL);

    if (pending_op != 0) {
        switch (pending_op) {
            case '+': stored_value += display_val; break;
            case '-': stored_value -= display_val; break;
            case '*': stored_value *= display_val; break;
            case '/':
                if (display_val != 0.0f) {
                    stored_value /= display_val;
                } else {
                    strcpy(display_buffer, "Error");
                    redraw_all();
                    return;
                }
                break;
        }
    } else {
        stored_value = display_val;
    }

    pending_op = op;
    new_entry = true;
    decimal_entered = false;

    redraw_all();
}

void button_equals(ui_button_t *button, void *user_data) {
    float display_val = strtof(display_buffer, NULL);

    switch (pending_op) {
        case '+': stored_value += display_val; break;
        case '-': stored_value -= display_val; break;
        case '*': stored_value *= display_val; break;
        case '/':
            if (display_val != 0.0) {
                stored_value /= display_val;
            } else {
                strcpy(display_buffer, "Error");
                redraw_all();
                return;
            }
            break;
        case 0: stored_value = display_val; break;
    }

    snprintf(display_buffer, MAX_DISPLAY, "%.8g", stored_value);
    display_pos = strlen(display_buffer);
    pending_op = 0;
    stored_value = 0.0;
    new_entry = true;
    decimal_entered = false;

    redraw_all();
}

void button_clear(ui_button_t *button, void *user_data) {
    strcpy(display_buffer, "0");
    display_pos = 1;
    current_value = 0.0;
    stored_value = 0.0;
    pending_op = 0;
    new_entry = true;
    decimal_entered = false;

    redraw_all();
}

void button_sign(ui_button_t *button, void *user_data) {
    float val = strtof(display_buffer, NULL);
    val = -val;
    snprintf(display_buffer, MAX_DISPLAY, "%.8g", val);
    display_pos = strlen(display_buffer);

    redraw_all();
}

void button_percent(ui_button_t *button, void *user_data) {
    float val = strtof(display_buffer, NULL);
    val = val / 100.0f;
    snprintf(display_buffer, MAX_DISPLAY, "%.8g", val);
    display_pos = strlen(display_buffer);
    new_entry = true;

    redraw_all();
}

void button_exit(ui_button_t *button, void *user_data) {
    os_load_app("launcher");
}

static void set_button_callback(ui_button_t *btn, const char *label) {
    if (strcmp(label, "C") == 0) {
        ui_button_set_callback(btn, button_clear, NULL);
    } else if (strcmp(label, "\u00B1") == 0) {
        ui_button_set_callback(btn, button_sign, NULL);
    } else if (strcmp(label, "%") == 0) {
        ui_button_set_callback(btn, button_percent, NULL);
    } else if (strcmp(label, "/") == 0 || strcmp(label, "*") == 0 ||
               strcmp(label, "-") == 0 || strcmp(label, "+") == 0) {
        char *op_data = (char *)malloc(1);
        *op_data = label[0];
        ui_button_set_callback(btn, button_operator, op_data);
    } else if (strcmp(label, "=") == 0) {
        ui_button_set_callback(btn, button_equals, NULL);
    } else if (strcmp(label, "\u2718") == 0) {
        ui_button_set_callback(btn, button_exit, NULL);
    } else if (strcmp(label, ".") == 0) {
        ui_button_set_callback(btn, button_decimal, NULL);
    } else if (strlen(label) == 1 && label[0] >= '0' && label[0] <= '9') {
        char *digit_data = (char *)malloc(1);
        *digit_data = label[0];
        ui_button_set_callback(btn, button_digit, digit_data);
    }
}

static void create_toolbars(void) {
    int screen_rows = text_mode_get_rows();
    int display_rows = 2;
    int available_rows = screen_rows - display_rows;
    int toolbar_height = available_rows / NUM_TOOLBARS;
    if (toolbar_height < 1) toolbar_height = 1;

    // Center toolbars vertically in the available space
    int total_height = NUM_TOOLBARS * toolbar_height;
    int start_y = display_rows + (available_rows - total_height) / 2;

    for (int row = 0; row < NUM_TOOLBARS; row++) {
        toolbars[row] = ui_toolbar_create(start_y + row * toolbar_height, toolbar_height, 4, toolbar_labels[row]);
        if (!toolbars[row]) continue;

        for (int col = 0; col < 4; col++) {
            ui_button_t *btn = ui_toolbar_get_button(toolbars[row], col);
            if (!btn) continue;
            ui_button_set_colors(btn, 0xFFFF, 0x0000);
            set_button_callback(btn, toolbar_labels[row][col]);
        }
    }
}

static void redraw_all(void) {
    draw_display();
    for (int i = 0; i < NUM_TOOLBARS; i++) {
        if (toolbars[i]) ui_toolbar_draw(toolbars[i]);
    }
}

void draw_display() {
    int screen_cols = text_mode_get_cols();
    int screen_rows = text_mode_get_rows();

    // Display takes the first 2 rows
    const int display_rows = 2;

    // Clear the display area
    for (int r = 0; r < display_rows && r < screen_rows; r++) {
        for (int c = 0; c < screen_cols; c++) {
            text_mode_print_at_attr_bg(c, r, " ", TEXT_COLOR_BRIGHT_GREEN, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
        }
    }

    // Align text to the button area of the first toolbar
    ui_button_t *first_btn = toolbars[0] ? ui_toolbar_get_button(toolbars[0], 0) : NULL;
    ui_button_t *last_btn = toolbars[0] ? ui_toolbar_get_button(toolbars[0], 3) : NULL;
    int left_x = first_btn ? first_btn->x : 0;
    int right_x = last_btn ? last_btn->x + last_btn->width - 1 : screen_cols - 1;
    int avail = right_x - left_x + 1;

    // Write the display buffer right-aligned within the button area
    int len = strlen(display_buffer);
    int start_col = right_x - len + 1;
    if (start_col < left_x) start_col = left_x;
    if (start_col > right_x) start_col = right_x;
    text_mode_print_at_attr_bg(start_col, display_rows - 1, display_buffer,
                               TEXT_COLOR_BRIGHT_GREEN, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD);
}

void app_init(app_context_t *ctx) {
    text_mode_init();
    text_mode_clear(0x0000);

    create_toolbars();
    redraw_all();
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_TOUCH) {
        for (int i = 0; i < NUM_TOOLBARS; i++) {
            if (toolbars[i] && ui_toolbar_handle_touch(toolbars[i], event)) {
                break;
            }
        }
    } else if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;

        if (key >= '0' && key <= '9') {
            char digit_data = key;
            button_digit(NULL, &digit_data);
        } else if (key == '.') {
            button_decimal(NULL, NULL);
        } else if (key == '+' || key == 8) {  // 8 is the BBQ20 keyboard + key
            char op_data = '+';
            button_operator(NULL, &op_data);
        } else if (key == '-') {
            char op_data = '-';
            button_operator(NULL, &op_data);
        } else if (key == '*') {
            char op_data = '*';
            button_operator(NULL, &op_data);
        } else if (key == '/') {
            char op_data = '/';
            button_operator(NULL, &op_data);
        } else if (key == '=' || key == '\r' || key == '\n') {
            button_equals(NULL, NULL);
        } else if (key == 'C' || key == 'c' || key == 27) {
            button_clear(NULL, NULL);
        } else if (key == 27 && strcmp(display_buffer, "Error") == 0) {
            button_exit(NULL, NULL);
        }
    }
}

void app_checkpoint(app_context_t *ctx) {
}

void app_close(app_context_t *ctx) {
    for (int i = 0; i < NUM_TOOLBARS; i++) {
        if (toolbars[i]) {
            ui_toolbar_destroy(toolbars[i]);
            toolbars[i] = NULL;
        }
    }
    text_mode_clear(0x0000);
}
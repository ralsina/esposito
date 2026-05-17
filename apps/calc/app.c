/*
 * Calculator App with UI Library
 * 4-function calculator with proper button layout
 */

#include "os_core.h"
#include "text_mode.h"
#include "ui.h"
#include "ui_button.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Declare external functions
extern double atof(const char *str);

#define MAX_DISPLAY 20

static char display_buffer[MAX_DISPLAY] = "0";
static int display_pos = 1;
static double current_value = 0.0;
static double stored_value = 0.0;
static char pending_op = 0;
static bool new_entry = true;
static bool decimal_entered = false;

static ui_button_t *buttons[20];
static int button_count = 0;

// Button layout: 4 columns x 5 rows
const char* button_labels[] = {
    "C", "±", "%", "/",
    "7", "8", "9", "*",
    "4", "5", "6", "-",
    "1", "2", "3", "+",
    "0", ".", "=", "+"
};

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

    for (int i = 0; i < button_count; i++) {
        ui_button_draw(buttons[i]);
    }
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

    for (int i = 0; i < button_count; i++) {
        ui_button_draw(buttons[i]);
    }
}

void button_operator(ui_button_t *button, void *user_data) {
    char op = *((char*)user_data);
    double display_val = atof(display_buffer);

    switch (pending_op) {
        case '+': stored_value += display_val; break;
        case '-': stored_value -= display_val; break;
        case '*': stored_value *= display_val; break;
        case '/':
            if (display_val != 0.0) {
                stored_value /= display_val;
            } else {
                strcpy(display_buffer, "Error");
                for (int i = 0; i < button_count; i++) {
                    ui_button_draw(buttons[i]);
                }
                return;
            }
            break;
        case 0: stored_value = display_val; break;
    }

    snprintf(display_buffer, MAX_DISPLAY, "%.8g", stored_value);
    display_pos = strlen(display_buffer);
    pending_op = op;
    new_entry = true;
    decimal_entered = false;

    for (int i = 0; i < button_count; i++) {
        ui_button_draw(buttons[i]);
    }
}

void button_equals(ui_button_t *button, void *user_data) {
    double display_val = atof(display_buffer);

    switch (pending_op) {
        case '+': stored_value += display_val; break;
        case '-': stored_value -= display_val; break;
        case '*': stored_value *= display_val; break;
        case '/':
            if (display_val != 0.0) {
                stored_value /= display_val;
            } else {
                strcpy(display_buffer, "Error");
                for (int i = 0; i < button_count; i++) {
                    ui_button_draw(buttons[i]);
                }
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

    for (int i = 0; i < button_count; i++) {
        ui_button_draw(buttons[i]);
    }
}

void button_clear(ui_button_t *button, void *user_data) {
    strcpy(display_buffer, "0");
    display_pos = 1;
    current_value = 0.0;
    stored_value = 0.0;
    pending_op = 0;
    new_entry = true;
    decimal_entered = false;

    for (int i = 0; i < button_count; i++) {
        ui_button_draw(buttons[i]);
    }
}

void button_sign(ui_button_t *button, void *user_data) {
    double val = atof(display_buffer);
    val = -val;
    snprintf(display_buffer, MAX_DISPLAY, "%.8g", val);
    display_pos = strlen(display_buffer);

    for (int i = 0; i < button_count; i++) {
        ui_button_draw(buttons[i]);
    }
}

void button_percent(ui_button_t *button, void *user_data) {
    double val = atof(display_buffer);
    val = val / 100.0;
    snprintf(display_buffer, MAX_DISPLAY, "%.8g", val);
    display_pos = strlen(display_buffer);
    new_entry = true;

    for (int i = 0; i < button_count; i++) {
        ui_button_draw(buttons[i]);
    }
}

void create_buttons() {
    int button_width = 6;
    int button_height = 3;
    int start_x = 2;
    int start_y = 7;

    // Clear existing buttons
    for (int i = 0; i < button_count; i++) {
        if (buttons[i]) {
            ui_button_destroy(buttons[i]);
        }
    }
    button_count = 0;

    // Create buttons in grid layout
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            int idx = row * 4 + col;
            int x = start_x + col * (button_width + 1);
            int y = start_y + row * (button_height + 1);

            ui_button_t *btn = ui_button_create(x, y, button_width, button_height, button_labels[idx]);
            ui_button_set_colors(btn, 0xFFFF, 0x0000);

            // Set callback based on button
            char key = button_labels[idx][0];
            if (key >= '0' && key <= '9') {
                char *digit_data = (char *)(char *)malloc(1);
                *digit_data = key;
                ui_button_set_callback(btn, button_digit, digit_data);
            } else if (key == '.') {
                ui_button_set_callback(btn, button_decimal, NULL);
            } else if (key == '+') {
                if (row == 1) { // Top row +
                    char *op_data = (char *)malloc(1);
                    *op_data = '+';
                    ui_button_set_callback(btn, button_operator, op_data);
                } else { // Bottom row (equals)
                    ui_button_set_callback(btn, button_equals, NULL);
                }
            } else if (key == '-') {
                char *op_data = (char *)malloc(1);
                *op_data = '-';
                ui_button_set_callback(btn, button_operator, op_data);
            } else if (key == '*') {
                char *op_data = (char *)malloc(1);
                *op_data = '*';
                ui_button_set_callback(btn, button_operator, op_data);
            } else if (key == '/') {
                char *op_data = (char *)malloc(1);
                *op_data = '/';
                ui_button_set_callback(btn, button_operator, op_data);
            } else if (key == '=') {
                ui_button_set_callback(btn, button_equals, NULL);
            } else if (key == 'C' || key == 'c') {
                ui_button_set_callback(btn, button_clear, NULL);
            } else if (idx == 1) { // +/- button
                ui_button_set_callback(btn, button_sign, NULL);
            } else if (key == '%') {
                ui_button_set_callback(btn, button_percent, NULL);
            }

            buttons[button_count++] = btn;
        }
    }
}

void draw_display() {
    // Clear display area
    for (int i = 2; i < 38; i++) {
        text_mode_print_at(i, 3, " ");
        text_mode_print_at(i, 4, " ");
    }

    // Draw display value
    int display_len = strlen(display_buffer);
    int start_x = 37 - display_len;
    if (start_x < 2) start_x = 2;

    text_mode_print_at(start_x, 4, display_buffer);
    text_mode_flush();
}

void app_init(app_context_t *ctx) {
    text_mode_init();
    text_mode_clear(0x0000);

    // Draw title
    text_mode_print_at(2, 0, "Calculator");
    text_mode_print_at(2, 1, "════════════════════════════");

    draw_display();
    create_buttons();
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_TOUCH) {
        for (int i = 0; i < button_count; i++) {
            if (ui_button_handle_touch(buttons[i], event)) {
                break; // Button handled the event
            }
        }
        draw_display();
    } else if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;

        // Keyboard support
        if (key >= '0' && key <= '9') {
            char digit_data = key;
            button_digit(NULL, &digit_data);
        } else if (key == '.') {
            button_decimal(NULL, NULL);
        } else if (key == '+') {
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
        }

        draw_display();
    }
}

void app_checkpoint(app_context_t *ctx) {
    // Save state if needed
}

void app_close(app_context_t *ctx) {
    // Cleanup buttons
    for (int i = 0; i < button_count; i++) {
        if (buttons[i]) {
            ui_button_destroy(buttons[i]);
        }
    }
    button_count = 0;
    text_mode_clear(0x0000);
}
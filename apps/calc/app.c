/*
 * Simple Calculator App
 * 4-function calculator with floating point support
 */

#include "os_core.h"
#include "text_mode.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// Declare external functions
extern double atof(const char *str);

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

#define MAX_INPUT 32

static float current_value = 0.0f;
static float stored_value = 0.0f;
static char pending_op = 0;
static char display_buffer[MAX_INPUT] = "0";
static int display_pos = 0;
static bool new_entry = true;

void update_display() {
    text_mode_clear(0x0000);

    // Draw calculator UI
    text_mode_print_at(0, 0, "╔════════════════════════════╗");
    text_mode_print_at(0, 1, "║      Calculator           ║");
    text_mode_print_at(0, 2, "╠════════════════════════════╣");
    text_mode_print_at(0, 3, "║                          ║");
    text_mode_print_at(0, 4, "║                          ║");
    text_mode_print_at(0, 5, "║                          ║");
    text_mode_print_at(0, 6, "╠════════════════════════════╣");
    text_mode_print_at(0, 7, "║ 7   8   9   /     C       ║");
    text_mode_print_at(0, 8, "║ 4   5   6   *     (      ║");
    text_mode_print_at(0, 9, "║ 1   2   3   -     )      ║");
    text_mode_print_at(0, 10, "║ 0   .   =   +    Enter   ║");
    text_mode_print_at(0, 11, "╚════════════════════════════╝");

    // Display current value
    text_mode_print_at(2, 3, display_buffer);
    text_mode_flush();
}

void clear_calc() {
    current_value = 0.0f;
    stored_value = 0.0f;
    pending_op = 0;
    strcpy(display_buffer, "0");
    display_pos = 0;
    new_entry = true;
    update_display();
}

void append_digit(char digit) {
    if (new_entry) {
        display_pos = 0;
        if (digit == '-') {
            display_buffer[display_pos++] = '-';
        } else {
            display_buffer[display_pos++] = digit;
        }
        display_buffer[display_pos] = '\0';
        new_entry = false;
    } else if (display_pos < MAX_INPUT - 1) {
        display_buffer[display_pos++] = digit;
        display_buffer[display_pos] = '\0';
    }
    update_display();
}

void append_decimal() {
    if (new_entry) {
        strcpy(display_buffer, "0.");
        display_pos = 2;
        new_entry = false;
    } else if (display_pos < MAX_INPUT - 1 && !strchr(display_buffer, '.')) {
        display_buffer[display_pos++] = '.';
        display_buffer[display_pos] = '\0';
    }
    update_display();
}

float get_display_value() {
    return atof(display_buffer);
}

void set_display_value(float value) {
    snprintf(display_buffer, MAX_INPUT, "%.8g", value);
    display_pos = strlen(display_buffer);
    current_value = value;
    new_entry = true;
    update_display();
}

void apply_operation() {
    float display_val = get_display_value();

    switch (pending_op) {
        case '+': stored_value += display_val; break;
        case '-': stored_value -= display_val; break;
        case '*': stored_value *= display_val; break;
        case '/':
            if (display_val != 0.0f) {
                stored_value /= display_val;
            } else {
                strcpy(display_buffer, "Error");
                update_display();
                return;
            }
            break;
        case 0: stored_value = display_val; break;
    }

    set_display_value(stored_value);
}

void handle_operation(char op) {
    apply_operation();
    pending_op = op;
}

void handle_equals() {
    apply_operation();
    pending_op = 0;
    stored_value = 0.0f;
}

void handle_clear() {
    clear_calc();
}

void app_init(app_context_t *ctx) {
    text_mode_init();
    clear_calc();
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;

        if (is_digit(key)) {
            append_digit(key);
        } else if (key == '.') {
            append_decimal();
        } else if (key == '+') {
            handle_operation('+');
        } else if (key == '-') {
            handle_operation('-');
        } else if (key == '*') {
            handle_operation('*');
        } else if (key == '/') {
            handle_operation('/');
        } else if (key == '=' || key == '\r' || key == '\n') {
            handle_equals();
        } else if (key == 'C' || key == 'c' || key == 27) { // Esc clears too
            handle_clear();
        }
    }
}

void app_checkpoint(app_context_t *ctx) {
    // Save state if needed
}

void app_close(app_context_t *ctx) {
    text_mode_clear(0x0000);
}
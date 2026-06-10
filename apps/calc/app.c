#include "os_core.h"
#include "text_mode.h"
#include "ui2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static ui2_screen_t *screen = NULL;

static const char *toolbar_labels[NUM_TOOLBARS][4] = {
    {"\xee\x82\x84", "\u00B1", "%", "/"},
    {"7", "8", "9", "*"},
    {"4", "5", "6", "-"},
    {"1", "2", "3", "+"},
    {"0", ".", "=", "\u2718"}
};

static void draw_display(void);
static void update_display(void);

static void button_digit(ui2_button_t *button, void *user_data) {
    (void)button;
    char digit = (char)(intptr_t)user_data;

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
}

static void button_decimal(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
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
}

static void button_operator(ui2_button_t *button, void *user_data) {
    (void)button;
    char op = (char)(intptr_t)user_data;
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
}

static void button_equals(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
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
}

static void button_clear(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    strcpy(display_buffer, "0");
    display_pos = 1;
    current_value = 0.0;
    stored_value = 0.0;
    pending_op = 0;
    new_entry = true;
    decimal_entered = false;
}

static void button_sign(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    float val = strtof(display_buffer, NULL);
    val = -val;
    snprintf(display_buffer, MAX_DISPLAY, "%.8g", val);
    display_pos = strlen(display_buffer);
}

static void button_percent(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    float val = strtof(display_buffer, NULL);
    val = val / 100.0f;
    snprintf(display_buffer, MAX_DISPLAY, "%.8g", val);
    display_pos = strlen(display_buffer);
    new_entry = true;
}

static void button_exit(ui2_button_t *button, void *user_data) {
    (void)button;
    (void)user_data;
    os_exit();
}

static void draw_display(void) {
    int cols = text_mode_get_cols();
    int val_len = strlen(display_buffer);
    int start_col = cols - 1 - val_len;
    if (start_col < 0) start_col = 0;
    text_mode_print_at_attr_bg(start_col, 1, display_buffer,
                               TEXT_COLOR_BRIGHT_GREEN, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD);
}

static void update_display(void) {
    if (screen) {
        ui2_screen_render(screen);
        draw_display();
    }
}

static void build_calc_screen(void) {
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    screen = ui2_screen_create();
    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);
    ui2_screen_set_root(screen, root);

    int display_rows = 2;
    int available_rows = rows - display_rows;
    int btn_h = available_rows / NUM_TOOLBARS;
    int total_height = NUM_TOOLBARS * btn_h;
    int start_y = display_rows + (available_rows - total_height) / 2;
    int btn_w = cols / 4;

    for (int row = 0; row < NUM_TOOLBARS; row++) {
        ui2_layout_t *bar = ui2_layout_create(0, start_y + row * btn_h, cols, btn_h, UI2_LAYOUT_HORIZONTAL);
        ui2_layout_set_gap(bar, 0);
        ui2_layout_add(root, UI2_WIDGET(bar));

        for (int col = 0; col < 4; col++) {
            const char *label = toolbar_labels[row][col];
            ui2_button_t *btn = ui2_button_create(0, 0, btn_w, btn_h, label);
            ui2_button_set_colors(btn, TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_BLUE);

            if (strcmp(label, "C") == 0) {
                ui2_button_set_callback(btn, button_clear, NULL);
            } else if (strcmp(label, "\u00B1") == 0) {
                ui2_button_set_callback(btn, button_sign, NULL);
            } else if (strcmp(label, "%") == 0) {
                ui2_button_set_callback(btn, button_percent, NULL);
            } else if (strcmp(label, ".") == 0) {
                ui2_button_set_callback(btn, button_decimal, NULL);
            } else if (strcmp(label, "=") == 0) {
                ui2_button_set_callback(btn, button_equals, NULL);
            } else if (strcmp(label, "\xee\x82\x84") == 0) {
                ui2_button_set_callback(btn, button_exit, NULL);
            } else if (label[0] == '+' || label[0] == '-' || label[0] == '*' || label[0] == '/') {
                ui2_button_set_callback(btn, button_operator, (void*)(intptr_t)label[0]);
            } else if (label[0] >= '0' && label[0] <= '9') {
                ui2_button_set_callback(btn, button_digit, (void*)(intptr_t)label[0]);
            }

            ui2_layout_add(bar, UI2_WIDGET(btn));
        }
    }
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH;
    text_mode_init();

    build_calc_screen();
    update_display();
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;
    bool changed = false;

    if (event->type == EVENT_TOUCH) {
        changed = ui2_screen_handle_event(screen, event);
    } else if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;

        if (key >= '0' && key <= '9') {
            button_digit(NULL, (void*)(intptr_t)key);
            changed = true;
        } else if (key == '.') {
            button_decimal(NULL, NULL);
            changed = true;
        } else if (key == '+' || key == 8) {
            button_operator(NULL, (void*)(intptr_t)'+');
            changed = true;
        } else if (key == '-') {
            button_operator(NULL, (void*)(intptr_t)'-');
            changed = true;
        } else if (key == '*') {
            button_operator(NULL, (void*)(intptr_t)'*');
            changed = true;
        } else if (key == '/') {
            button_operator(NULL, (void*)(intptr_t)'/');
            changed = true;
        } else if (key == '=' || key == '\r' || key == '\n') {
            button_equals(NULL, NULL);
            changed = true;
        } else if (key == 'C' || key == 'c' || key == 27) {
            button_clear(NULL, NULL);
            changed = true;
        }
    }

    if (changed) update_display();
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    if (screen) {
        ui2_screen_destroy(screen);
        screen = NULL;
    }
}

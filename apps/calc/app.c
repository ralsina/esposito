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
extern float strtof(const char *str, char **endptr);

#define MAX_DISPLAY 20

static char display_buffer[MAX_DISPLAY] = "0";
static int display_pos = 1;
static float current_value = 0.0f;
static float stored_value = 0.0f;
static char pending_op = 0;
static bool new_entry = true;
static bool decimal_entered = false;



static ui_button_t *buttons[20];
static int button_count = 0;

// Button layout: 4 columns x 5 rows
// Standard calculator layout
const char* button_labels[] = {
    "C", "+/-", "%", "/",
    "7", "8", "9", "*",
    "4", "5", "6", "-",
    "1", "2", "3", "+",
    "0", ".", "", "="  // Empty spot for better layout
};

// Button callback functions
void button_digit(ui_button_t *button, void *user_data) {
    char digit = *((char*)user_data);
    printf("button_digit: '%c'\n", digit);

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
    printf("button_decimal\n");
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
    printf("button_operator: '%c' (pending_op was '%c')\n", op, pending_op);
    float display_val = strtof(display_buffer, NULL);

    // If there's already a pending operation, calculate it first
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
                    for (int i = 0; i < button_count; i++) {
                        ui_button_draw(buttons[i]);
                    }
                    return;
                }
                break;
        }
    } else {
        // No pending operation, store this value
        stored_value = display_val;
    }

    pending_op = op;
    new_entry = true;
    decimal_entered = false;

    // Don't update display - keep showing the current number
    for (int i = 0; i < button_count; i++) {
        ui_button_draw(buttons[i]);
    }
}

void button_equals(ui_button_t *button, void *user_data) {
    printf("button_equals: pending_op='%c', stored_value=%.8g, display_buffer='%s'\n",
           pending_op, stored_value, display_buffer);
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
    printf("button_clear\n");
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
    float val = strtof(display_buffer, NULL);
    val = -val;
    snprintf(display_buffer, MAX_DISPLAY, "%.8g", val);
    display_pos = strlen(display_buffer);

    for (int i = 0; i < button_count; i++) {
        ui_button_draw(buttons[i]);
    }
}

void button_percent(ui_button_t *button, void *user_data) {
    float val = strtof(display_buffer, NULL);
    val = val / 100.0f;
    snprintf(display_buffer, MAX_DISPLAY, "%.8g", val);
    display_pos = strlen(display_buffer);
    new_entry = true;

    for (int i = 0; i < button_count; i++) {
        ui_button_draw(buttons[i]);
    }
}

void create_buttons() {
    // Get actual screen dimensions
    int screen_cols = text_mode_get_cols();
    int screen_rows = text_mode_get_rows();

    // Calculate available space for buttons (minimum 4 columns wide, 5 rows tall)
    int min_button_cols = 4;
    int min_button_rows = 5;
    int min_width = min_button_cols * 2;  // Each button at least 1 char wide + 1 space
    int min_height = min_button_rows * 1.5; // Reduced height requirement for tight spacing

    if (screen_cols < min_width || screen_rows < min_height + 6) {
        // Screen too small, cannot fit calculator
        return;
    }

    // Calculate available width for button area
    int available_width = screen_cols;
    int start_x = 0;  // Left-aligned for narrow screens
    
    // Dynamic vertical positioning - anchor to bottom, leave extra space at top
    int display_gap = 2;  // Space between display and buttons
    int local_vertical_gap = 1; // Gap between button rows
    int bottom_margin = 0; // No margin at bottom - buttons flush with edge
    
    // Set button height based on screen size
    int local_button_height = 2;  // Reduced height for smaller screens
    if (screen_rows >= 15) {
        local_button_height = 3;  // Use taller buttons on larger screens
    }
    
    // Calculate button positioning from bottom up
    int buttons_height = 5 * (local_button_height + local_vertical_gap) - local_vertical_gap;
    int local_button_start_y = screen_rows - bottom_margin - buttons_height;  // Where buttons start
    
    // Update global variables for display positioning
    extern int button_height;
    extern int button_start_y; 
    extern int vertical_gap;
    button_height = local_button_height;
    button_start_y = local_button_start_y;
    vertical_gap = local_vertical_gap;

    // Dynamic button sizing
    int normal_button_width;
    int equals_button_width;

    if (available_width >= 40) {
        // Full layout - maintain original proportions
        start_x = (available_width - 40) / 2;  // Center on wider screens
        normal_button_width = 9;
        equals_button_width = 19;
    } else {
        // Narrow screen - pack tightly
        int button_area_width = available_width - 2;  // 1 char margin each side
        int spacing = 1;  // Single space between buttons
        int buttons_per_row = 4;
        
        // Calculate minimum button width that can fit
        int min_individual_width = 1;  // Each button can be as small as 1 char
        int spacing_needed = (buttons_per_row - 1) * spacing;
        int total_min_width = buttons_per_row * min_individual_width + spacing_needed;
        
        if (button_area_width < total_min_width) {
            // Must use single character buttons
            normal_button_width = 1;
            equals_button_width = 1;  // Will be handled specially
            start_x = 0;  // No centering possible
        } else {
            // Distribute available space among buttons
            int extra_space = button_area_width - total_min_width;
            normal_button_width = min_individual_width + extra_space / buttons_per_row;
            
            // Make sure normal_button_width is at least 1
            if (normal_button_width < 1) normal_button_width = 1;
            
            // Equals button gets more space when possible
            if (normal_button_width > 1) {
                equals_button_width = normal_button_width * 2 + spacing;
            } else {
                equals_button_width = 1;
            }
        }
    }

    // Clear existing buttons
    for (int i = 0; i < button_count; i++) {
        if (buttons[i]) {
            ui_button_destroy(buttons[i]);
        }
    }
    button_count = 0;

printf("Screen: %dx%d, Layout at start_x=%d, button_start_y=%d\n",
           screen_cols, screen_rows, start_x, button_start_y);
    printf("Normal button size: %dx%d, Equals button: %dx%d\n",
           normal_button_width, button_height, equals_button_width, button_height);
    printf("Display gap: %d, vertical gap: %d, button height: %d\n",
           display_gap, vertical_gap, button_height);

    // Clear existing buttons
    for (int i = 0; i < button_count; i++) {
        if (buttons[i]) {
            ui_button_destroy(buttons[i]);
        }
    }
    button_count = 0;

    // Create buttons in grid layout
    for (int row = 0; row < 5; row++) {
        int col = 0;
        for (int idx = row * 4; idx < row * 4 + 4; idx++) {
            const char *label = button_labels[idx];
            
            // Skip empty button (for layout)
            if (strcmp(label, "") == 0) {
                continue;
            }

            // Calculate position
            int x = start_x + col * (normal_button_width + 1);
            int y = button_start_y + row * (button_height + vertical_gap);
            int width = normal_button_width;

            // Last row has special widths
            if (row == 4) {
                if (strcmp(label, "=") == 0) {
                    width = equals_button_width;
                    // Adjust x position for the wider equals button
                    if (normal_button_width > 1) {
                        x = start_x + normal_button_width + normal_button_width + 2;  // After 0 and .
                    } else {
                        x = start_x + 2;  // Position after 0 and . on single-char layout
                    }
                }
            }

            printf("Button[%d] '%s' at x=%d, y=%d, width=%d\n", idx, label, x, y, width);

            ui_button_t *btn = ui_button_create(x, y, width, button_height, label);
            ui_button_set_colors(btn, 0xFFFF, 0x0000);

            // Set callback based on button label
            if (strcmp(label, "C") == 0) {
                ui_button_set_callback(btn, button_clear, NULL);
            } else if (strcmp(label, "+/-") == 0) {
                ui_button_set_callback(btn, button_sign, NULL);
            } else if (strcmp(label, "%") == 0) {
                ui_button_set_callback(btn, button_percent, NULL);
            } else if (strcmp(label, "/") == 0 || strcmp(label, "*") == 0 ||
                       strcmp(label, "-") == 0 || strcmp(label, "+") == 0) {
                // Operator buttons
                char *op_data = (char *)malloc(1);
                *op_data = label[0];
                ui_button_set_callback(btn, button_operator, op_data);
            } else if (strcmp(label, "=") == 0) {
                ui_button_set_callback(btn, button_equals, NULL);
            } else if (strcmp(label, ".") == 0) {
                ui_button_set_callback(btn, button_decimal, NULL);
            } else if (strlen(label) == 1 && label[0] >= '0' && label[0] <= '9') {
                // Digit buttons
                char *digit_data = (char *)malloc(1);
                *digit_data = label[0];
                ui_button_set_callback(btn, button_digit, digit_data);
            }

            buttons[button_count++] = btn;
            col++;
        }
    }
}

// Global variables for button layout coordination
int button_start_y = 0;
int button_height = 3;
int vertical_gap = 1;

void draw_display() {
    extern void display_fill_rect(int x, int y, int width, int height, uint16_t color);
    extern void display_draw_scaled_text_bg(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale);
    extern int text_mode_get_char_width(void);
    extern int text_mode_get_char_height(void);

    int char_width = text_mode_get_char_width();
    int char_height = text_mode_get_char_height();
    int screen_cols = text_mode_get_cols();
    int screen_rows = text_mode_get_rows();

    // Dynamic display sizing - positioned above buttons
    int display_width, display_x, display_y;
    
    if (screen_cols >= 40) {
        // Full display on wider screens
        display_width = 36 * char_width;
        display_x = ((text_mode_get_cols() * char_width) - (40 * char_width)) / 2 + (2 * char_width);
        display_y = 1 * char_height;  // Top row only
    } else {
        // Narrow screen - position display just above buttons
        display_width = (screen_cols - 2) * char_width;  // Minimal margins
        display_x = 1 * char_width;  // Left-align with minimal margin
        // Position display just above buttons
        display_y = button_start_y - 2;  // 2 rows above buttons
    }
    
    int display_height = 2 * char_height;

    // Clear display area with black background
    display_fill_rect(display_x, display_y, display_width, display_height, 0x0000);

    // Measure text to right-align it
    extern void display_measure_scaled_text(const char *text, int scale, int *width, int *height);
    int text_width, text_height;
    
    // Adjust scale based on display width and screen size
    int text_scale;
    if (display_width >= 30 * char_width) {
        text_scale = 3;
    } else if (display_width >= 20 * char_width) {
        text_scale = 2;
    } else {
        text_scale = 1;  // Single character display on very small screens
    }
    display_measure_scaled_text(display_buffer, text_scale, &text_width, &text_height);

    // Calculate position to right-align the text
    int text_x = display_x + display_width - text_width - (char_width / 2);
    int text_y = display_y + (display_height - text_height) / 2;

    // Draw the display value in bright green with calculated scale
    display_draw_scaled_text_bg(text_x, text_y, display_buffer, 0x07E0, 0x0000, text_scale);
}

void app_init(app_context_t *ctx) {
    text_mode_init();
    text_mode_clear(0x0000);

    draw_display();
    create_buttons();

    // Draw all buttons
    for (int i = 0; i < button_count; i++) {
        ui_button_draw(buttons[i]);
    }
    text_mode_flush();
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_TOUCH) {
        // UI widgets handle pixel-to-character conversion internally
        // Pass the original pixel coordinates directly

        printf("Touch at pixels x=%d, y=%d\n", event->touch.x, event->touch.y);

        for (int i = 0; i < button_count; i++) {
            if (ui_button_handle_touch(buttons[i], event)) {
                printf("Button %d clicked\n", i);
                break; // Button handled the event
            }
        }
        draw_display();
    } else if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;
        printf("Keyboard key: %d (0x%x) '%c'\n", key, key, key);

        // Keyboard support - note: some keys don't send ASCII codes
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
            // 27 is ESC key
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
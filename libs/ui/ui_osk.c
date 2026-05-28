#include "ui_osk.h"
#include "ui_button.h"
#include "ui_toolbar.h"
#include "hardware.h"
#include "os_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *TAG = "ui_osk";

#define MAX_INPUT_LENGTH 256
#define MAX_BUTTONS 50
#define KEYBOARD_ROWS 4
#define KEYBOARD_COLS 10

// OSK state
typedef struct {
    char input_buffer[MAX_INPUT_LENGTH];
    int cursor_pos;
    bool shift_active;
    bool symbol_mode;
    bool mask_input;
    bool is_active;

    // Use toolbars instead of individual buttons
    ui_toolbar_t *keyboard_bars[5]; // 5 keyboard rows
    char *original_labels[MAX_BUTTONS]; // Still need for shift handling
    int total_button_count;

    int title_y;
    int input_y;
    int keyboard_start_y;
    int input_display_width;

    char *user_buffer;       // User's result buffer
    int max_len;             // Maximum buffer length
    ui_osk_result_t result;  // Final result

    text_mode_snapshot_t *saved_screen;

    bool needs_redraw;
} osk_state_t;

// Global result storage
static ui_osk_result_t g_last_osk_result = UI_OSK_RESULT_CANCELLED;
static osk_state_t *g_osk_state = NULL;

// Forward declarations
static void create_keyboard_layout(osk_state_t *state);
static void destroy_keyboard_layout(osk_state_t *state);
static void draw_input_display(osk_state_t *state, const char *title);
static void handle_key_press(osk_state_t *state, const char *key);
static void finish_osk(ui_osk_result_t result);
static char get_shifted_char(char c);
static void update_button_label(osk_state_t *state, int button_index);

// 4x10 Ortholinear keyboard layout
// Row 1: Numbers 1-0
// Row 2: QWERTYUIOP
// Row 3: ASDFGHJKL;
// Row 4: ZXCVBNM,./
// Special keys: SHIFT, BSP (backspace), ENT (enter), ESC, SPACE

static const char *keyboard_layout[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
    {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
    {"a", "s", "d", "f", "g", "h", "j", "k", "l", ";"},
    {"z", "x", "c", "v", "b", "n", "m", ",", ".", "/"}
};

// Special key definitions
static const struct {
    const char *label;
    const char *key;
} special_keys[] = {
    {"⇧", "SHIFT"},
    {"⌫", "BSP"},
    {"⏎", "ENT"},
    {"\xE2\x9C\x98", "ESC"},
    {"_", "SPACE"},
    {NULL, NULL}
};

// Main OSK implementation
bool ui_osk_input_text(
    const char *title,
    char *buffer,
    int max_len,
    const char *initial_text,
    bool mask_input
) {
    if (!buffer || max_len <= 0) {
        return false;
    }

    // Wait for any existing OSK session
    while (g_osk_state != NULL) {
        return false;
    }

    // Allocate OSK state
    osk_state_t *state = (osk_state_t*)calloc(1, sizeof(osk_state_t));
    if (!state) {
        return false;
    }

    // Initialize state
    state->total_button_count = 0;
    state->cursor_pos = 0;
    state->shift_active = false;
    state->symbol_mode = false;
    state->mask_input = mask_input;
    state->is_active = true;
    state->user_buffer = buffer;
    state->max_len = max_len;
    state->result = UI_OSK_RESULT_CANCELLED;
    state->needs_redraw = false;

    // Initialize input buffer
    if (initial_text) {
        strncpy(state->input_buffer, initial_text, MAX_INPUT_LENGTH - 1);
        state->input_buffer[MAX_INPUT_LENGTH - 1] = '\0';
        state->cursor_pos = strlen(state->input_buffer);
    } else {
        state->input_buffer[0] = '\0';
        state->cursor_pos = 0;
    }

    // Save current screen
    state->saved_screen = text_mode_save_snapshot();
    if (!state->saved_screen) {
        free(state);
        return false;
    }

    // Calculate screen layout
    int screen_rows = text_mode_get_rows();
    state->title_y = 0;
    state->input_y = 1;
    state->keyboard_start_y = 3;
    state->input_display_width = text_mode_get_cols();

    // Clear screen
    text_mode_clear(TEXT_COLOR_BLACK);

    // Create keyboard layout
    create_keyboard_layout(state);

    // Draw initial input display
    draw_input_display(state, title);

    // Set global state
    g_osk_state = state;

    return true;
}

// Event handler for OSK
bool ui_osk_handle_event(app_context_t *ctx, event_t *event) {
    if (!g_osk_state || !g_osk_state->is_active) {
        return false;
    }

    osk_state_t *state = g_osk_state;

    // Handle touch events
    if (event->type == EVENT_TOUCH && event->touch.pressed) {
        // First check if touch is in input area for cursor positioning
        int input_area_height = 3; // Input area + spacing
        int char_width = text_mode_get_char_width();
        int char_height = text_mode_get_char_height();
        int touch_col = event->touch.x / char_width;
        int touch_row = event->touch.y / char_height;

        if (touch_row >= state->input_y && touch_row < state->input_y + input_area_height) {
            // Check if touch is within the input box area
            if (touch_col >= 1 && touch_col < state->input_display_width - 1) {
                // Calculate new cursor position based on touch position
                const char *display_text = state->mask_input ? "****************" : state->input_buffer;
                int text_len = strlen(display_text);
                int max_display_width = state->input_display_width - 2;

                // Calculate where the text starts (centered)
                int text_start_x = 1 + (max_display_width - text_len) / 2;

                // Calculate new cursor position
                int new_cursor_pos = touch_col - text_start_x;
                if (new_cursor_pos < 0) {
                    new_cursor_pos = 0;
                } else if (new_cursor_pos > text_len) {
                    new_cursor_pos = text_len; // Can move to end of text
                }

                state->cursor_pos = new_cursor_pos;
                // Don't truncate the string! Just move the cursor position

                draw_input_display(state, ""); // Immediate redraw
                return true;
            }
        }

        // Check each toolbar for button hits
        for (int row = 0; row < 5; row++) {
            if (state->keyboard_bars[row] && ui_toolbar_handle_touch(state->keyboard_bars[row], event)) {
                // Find which button was actually hit by checking each button in the toolbar
                int row_button_count = 0;
                if (row == 0) row_button_count = 10;      // Numbers row
                else if (row == 1) row_button_count = 10; // QWERTY row  
                else if (row == 2) row_button_count = 10; // ASDF row
                else if (row == 3) row_button_count = 10; // ZXCV row
                else if (row == 4) row_button_count = 5;  // Special row
                
                // Check each button in this toolbar to see if it was the one hit
                for (int col = 0; col < row_button_count; col++) {
                    ui_button_t *button = ui_toolbar_get_button(state->keyboard_bars[row], col);
                    if (button && ui_button_handle_touch(button, event)) {
                        // Calculate the global button index
                        int global_button_index = 0;
                        for (int r = 0; r < row; r++) {
                            if (r == 0) global_button_index += 10;  // Numbers
                            else if (r == 1) global_button_index += 10; // QWERTY
                            else if (r == 2) global_button_index += 10; // ASDF
                            else if (r == 3) global_button_index += 10; // ZXCV
                            else if (r == 4) global_button_index += 5;  // Special
                        }
                        global_button_index += col;
                        
                        if (global_button_index < state->total_button_count) {
                            const char *original_label = state->original_labels[global_button_index];
                             
// Handle special keys directly
                             if (strcmp(original_label, "⏎") == 0) {
                                 finish_osk(UI_OSK_RESULT_CONFIRMED);
                             } else if (strcmp(original_label, "\xE2\x9C\x98") == 0) {
                                 finish_osk(UI_OSK_RESULT_CANCELLED);
                             } else if (strcmp(original_label, "⇧") == 0) {
                                // Toggle shift mode
                                state->shift_active = !state->shift_active;
                                for (int i = 0; i < state->total_button_count; i++) {
                                    update_button_label(state, i);
                                }
                                for (int r = 0; r < 5; r++) {
                                    if (state->keyboard_bars[r]) {
                                        ui_toolbar_draw(state->keyboard_bars[r]);
                                    }
                                }
                            } else if (strcmp(original_label, "#@") == 0) {
                                // Toggle symbol mode
                                state->symbol_mode = !state->symbol_mode;
                                for (int i = 0; i < state->total_button_count; i++) {
                                    update_button_label(state, i);
                                }
                                for (int r = 0; r < 5; r++) {
                                    if (state->keyboard_bars[r]) {
                                        ui_toolbar_draw(state->keyboard_bars[r]);
                                    }
                                }
                            } else if (strcmp(original_label, "⌫") == 0) {
                                // Handle backspace
                                if (state->cursor_pos > 0) {
                                    state->cursor_pos--;
                                    state->input_buffer[state->cursor_pos] = '\0';
                                    draw_input_display(state, "");
                                }
                            } else if (strcmp(original_label, "_") == 0) {
                                // Handle space
                                if (state->cursor_pos < MAX_INPUT_LENGTH - 1) {
                                    state->input_buffer[state->cursor_pos++] = ' ';
                                    state->input_buffer[state->cursor_pos] = '\0';
                                    draw_input_display(state, "");
                                }
                            } else {
                                // Handle normal keys - calculate based on current mode
                                const char *symbol_keys[4][10] = {
                                    {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"},
                                    {"-", "_", "=", "+", "[", "]", "{", "}", "|", "\\"},
                                    {":", ";", "'", "\"", "<", ">", ",", ".", "?", "/"},
                                    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"}
                                };
                                
                                const char *main_keys[4][10] = {
                                    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
                                    {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
                                    {"a", "s", "d", "f", "g", "h", "j", "k", "l", ";"},
                                    {"z", "x", "c", "v", "b", "n", "m", ",", ".", "/"}
                                };
                                
                                // Calculate row and column from button index
                                int key_row = 0;
                                int key_col = 0;
                                if (global_button_index < 10) {
                                    key_row = 0;
                                    key_col = global_button_index;
                                } else if (global_button_index < 20) {
                                    key_row = 1;
                                    key_col = global_button_index - 10;
                                } else if (global_button_index < 30) {
                                    key_row = 2;
                                    key_col = global_button_index - 20;
                                } else if (global_button_index < 40) {
                                    key_row = 3;
                                    key_col = global_button_index - 30;
                                } else {
                                    return true; // Special keys handled above
                                }
                                
                                char c = '\0';
                                if (state->symbol_mode) {
                                    // Use symbol keyboard
                                    c = symbol_keys[key_row][key_col][0];
                                    
                                    // Apply shift to symbols if needed
                                    if (state->shift_active) {
                                        c = get_shifted_char(c);
                                    }
                                } else {
                                    // Use main QWERTY keyboard
                                    c = main_keys[key_row][key_col][0];
                                    
                                    // Apply shift to letters if needed
                                    if (c >= 'a' && c <= 'z') {
                                        c = state->shift_active ? (char)toupper(c) : c;
                                    } else if (c >= '0' && c <= '9' && state->shift_active) {
                                        const char *shifted_symbols = ")!@#$%^&*(";
                                        c = shifted_symbols[c - '0'];
                                    } else if (state->shift_active) {
                                        c = get_shifted_char(c);
                                    }
                                }
                                
                                // Add character to buffer
                                if (state->cursor_pos < MAX_INPUT_LENGTH - 1) {
                                    int current_len = strlen(state->input_buffer);
                                    if (state->cursor_pos < current_len) {
                                        // Shift characters to make room
                                        for (int i = current_len; i >= state->cursor_pos; i--) {
                                            state->input_buffer[i + 1] = state->input_buffer[i];
                                        }
                                    }
                                    
                                    state->input_buffer[state->cursor_pos++] = c;
                                    state->input_buffer[state->cursor_pos] = '\0';
                                     draw_input_display(state, "");
                                 }
                             }
 
                             return true;
                         }
                     }
                 }
             }
         }
     } else if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;

        // Handle physical keyboard input
        if (key == 27) { // ESC
            finish_osk(UI_OSK_RESULT_CANCELLED);
            return true;
        } else if (key == '\n' || key == '\r') { // Enter
            finish_osk(UI_OSK_RESULT_CONFIRMED);
            return true;
        } else if (key == '\b' || key == 127) { // Backspace
            if (state->cursor_pos > 0) {
                state->cursor_pos--;
                state->input_buffer[state->cursor_pos] = '\0';
                draw_input_display(state, ""); // Immediate redraw
            }
            return true;
        } else if (key == 0x11 || key == 0x12 || key == 0x13 || key == 0x14) { // SHIFT/CTRL/ALT keys
            // Handle SHIFT key toggle (QWERTY keyboard)
            if (key == 0x11 || key == 0x12) { // Left or Right SHIFT
                state->shift_active = !state->shift_active;

                // Update all button labels to reflect shift state
                for (int i = 0; i < state->total_button_count; i++) {
                    update_button_label(state, i);
                }
                return true;
            }
        } else if (key >= 32 && key <= 126) { // Printable character
            char char_to_add = key;

            // Apply shift if active
            if (state->shift_active) {
                char_to_add = get_shifted_char(key);
            }

            if (state->cursor_pos < MAX_INPUT_LENGTH - 1) {
                // Insert character at cursor position
                int current_len = strlen(state->input_buffer);
                if (state->cursor_pos < current_len) {
                    // Shift characters to make room
                    for (int i = current_len; i >= state->cursor_pos; i--) {
                        state->input_buffer[i + 1] = state->input_buffer[i];
                    }
                }

                state->input_buffer[state->cursor_pos++] = char_to_add;
                if (strlen(state->input_buffer) >= MAX_INPUT_LENGTH - 1) {
                    state->input_buffer[MAX_INPUT_LENGTH - 1] = '\0';
                }
                draw_input_display(state, ""); // Immediate redraw
            }
            return true;
        }
    }

    return false;
}

// Check if OSK is currently active
bool ui_osk_is_active(void) {
    return g_osk_state != NULL && g_osk_state->is_active;
}

// Get the result of the last OSK session
ui_osk_result_t ui_osk_get_result(void) {
    return g_last_osk_result;
}

// Finish OSK session
static void finish_osk(ui_osk_result_t result) {
    if (!g_osk_state) return;

    osk_state_t *state = g_osk_state;
    state->is_active = false;
    state->result = result;

    // Store result globally
    g_last_osk_result = result;

    // Copy result to user buffer if confirmed
    if (result == UI_OSK_RESULT_CONFIRMED && state->user_buffer) {
        strncpy(state->user_buffer, state->input_buffer, state->max_len - 1);
        state->user_buffer[state->max_len - 1] = '\0';
    }

    // Clean up keyboard layout
    destroy_keyboard_layout(state);

    // Restore screen
    if (state->saved_screen) {
        text_mode_restore_snapshot(state->saved_screen);
        text_mode_free_snapshot(state->saved_screen);
        state->saved_screen = NULL;
    }

    // Free state
    free(state);
    g_osk_state = NULL;
}

// Get shifted character
static char get_shifted_char(char c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }
    if (c >= '0' && c <= '9') {
        const char *shifted = ")!@#$%^&*(";
        return shifted[c - '0'];
    }
    switch (c) {
        case ';': return ':';
        case ',': return '<';
        case '.': return '>';
        case '/': return '?';
        case '`': return '~';
        case '-': return '_';
        case '=': return '+';
        case '[': return '{';
        case ']': return '}';
        case '\\': return '|';
        case '\'': return '"';
        default: return c;
    }
}

// Handle key press
static void handle_key_press(osk_state_t *state, const char *key) {
    if (!key) return;

    // Handle SHIFT key
    if (strcmp(key, "⇧") == 0) {
        state->shift_active = !state->shift_active;

        // Update all button labels to reflect shift state
        for (int i = 0; i < state->total_button_count; i++) {
            update_button_label(state, i);
        }
        
        // Redraw all toolbars after all labels have been updated
        for (int row = 0; row < 5; row++) {
            if (state->keyboard_bars[row]) {
                ui_toolbar_draw(state->keyboard_bars[row]);
            }
        }
        return;
    }
    
    // Handle SYMBOL key
    if (strcmp(key, "#@") == 0) {
        state->symbol_mode = !state->symbol_mode;
        
        // Update all button labels to reflect symbol mode
        for (int i = 0; i < state->total_button_count; i++) {
            update_button_label(state, i);
        }
        
        // Redraw all toolbars after all labels have been updated
        for (int row = 0; row < 5; row++) {
            if (state->keyboard_bars[row]) {
                ui_toolbar_draw(state->keyboard_bars[row]);
            }
        }
        return;
    }

    // Handle backspace
    if (strcmp(key, "⌫") == 0) {
        if (state->cursor_pos > 0) {
            state->cursor_pos--;
            state->input_buffer[state->cursor_pos] = '\0';
            state->needs_redraw = true;
        }
        return;
    }

    // Handle space
    if (strcmp(key, "_") == 0) {
        if (state->cursor_pos < MAX_INPUT_LENGTH - 1) {
            state->input_buffer[state->cursor_pos++] = ' ';
            state->input_buffer[state->cursor_pos] = '\0';
            state->needs_redraw = true;
        }
        return;
    }

    // Handle single character keys
    if (strlen(key) == 1) {
        char c = key[0];

        // Apply shift if active
        if (state->shift_active) {
            c = get_shifted_char(c);
        }

        // Add character to buffer at cursor position
        if (state->cursor_pos < MAX_INPUT_LENGTH - 1) {
            int current_len = strlen(state->input_buffer);
            if (state->cursor_pos < current_len) {
                // Shift characters to make room
                for (int i = current_len; i >= state->cursor_pos; i--) {
                    state->input_buffer[i + 1] = state->input_buffer[i];
                }
            }

            state->input_buffer[state->cursor_pos++] = c;
            if (strlen(state->input_buffer) >= MAX_INPUT_LENGTH - 1) {
                state->input_buffer[MAX_INPUT_LENGTH - 1] = '\0';
            }
            state->needs_redraw = true;
        }
    }
}

// Update a single button's label based on shift state
static void update_button_label(osk_state_t *state, int button_index) {
    if (button_index < 0 || button_index >= state->total_button_count) {
        return;
    }

    // Find the button in the toolbar system
    ui_button_t *button = NULL;
    int current_button_index = 0;
    
    for (int row = 0; row < 5; row++) {
        if (state->keyboard_bars[row]) {
            int row_button_count = 0;
            if (row == 0) row_button_count = 10;
            else if (row == 1) row_button_count = 10;
            else if (row == 2) row_button_count = 10;
            else if (row == 3) row_button_count = 10;
            else if (row == 4) row_button_count = 5;
            
            for (int col = 0; col < row_button_count; col++) {
                if (current_button_index == button_index) {
                    button = ui_toolbar_get_button(state->keyboard_bars[row], col);
                    break;
                }
                current_button_index++;
            }
            if (button) break;
        }
    }

    const char *original_label = state->original_labels[button_index];

    if (!button || !original_label) {
        return;
    }

    // Handle SHIFT key visual
    if (strcmp(original_label, "⇧") == 0) {
        if (state->shift_active) {
            ui_button_set_colors(button, TEXT_COLOR_YELLOW, TEXT_COLOR_RED);
        } else {
            ui_button_set_colors(button, TEXT_COLOR_WHITE, TEXT_COLOR_BLUE);
        }
        // Don't redraw here - let the caller handle redrawing all toolbars
        return;
    }
    
    // Handle SYMBOL key visual
    if (strcmp(original_label, "#@") == 0) {
        if (state->symbol_mode) {
            ui_button_set_colors(button, TEXT_COLOR_YELLOW, TEXT_COLOR_GREEN);
        } else {
            ui_button_set_colors(button, TEXT_COLOR_WHITE, TEXT_COLOR_BLUE);
        }
        // Don't redraw here - let the caller handle redrawing all toolbars
        return;
    }

    // Skip special keys
    if (strcmp(original_label, "_") == 0 ||
        strcmp(original_label, "⏎") == 0 ||
        strcmp(original_label, "\xE2\x9C\x98") == 0 ||
        strcmp(original_label, "⌫") == 0) {
        return;
    }

    // Only handle single-character keys
    if (strlen(original_label) == 1) {
        char c = original_label[0];
        char new_char = c;
        
        // Get the current row and column from button_index
        int current_row = 0;
        int current_col = 0;
        if (button_index < 10) {
            current_row = 0;
            current_col = button_index;
        } else if (button_index < 20) {
            current_row = 1;
            current_col = button_index - 10;
        } else if (button_index < 30) {
            current_row = 2;
            current_col = button_index - 20;
        } else if (button_index < 40) {
            current_row = 3;
            current_col = button_index - 30;
        } else {
            return; // Special keys
        }
        
        // If in symbol mode, use symbol keyboard
        if (state->symbol_mode) {
            static const char *symbol_keys[4][10] = {
                {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"},
                {"-", "_", "=", "+", "[", "]", "{", "}", "|", "\\"},
                {":", ";", "'", "\"", "<", ">", ",", ".", "?", "/"},
                {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"}
            };
            
            if (current_row < 4) {
                new_char = symbol_keys[current_row][current_col][0];
                
                // Apply shift to symbols if needed
                if (state->shift_active) {
                    new_char = get_shifted_char(new_char);
                }
            }
        } else {
            // Normal QWERTY keyboard
            // Calculate what the label should be
            if (c >= 'a' && c <= 'z') {
                new_char = state->shift_active ? (char)toupper(c) : c;
            } else if (c >= '0' && c <= '9' && state->shift_active) {
                const char *shifted_symbols = ")!@#$%^&*(";
                new_char = shifted_symbols[c - '0'];
            } else if (state->shift_active) {
                new_char = get_shifted_char(c);
            }
        }

        // Update button label if it needs to change
        char new_label[2] = {new_char, '\0'};
        ui_button_set_text(button, new_label);
        // Redraw the toolbar that contains this button
        // Calculate which toolbar this button belongs to based on button_index
        int toolbar_row = 0;
        if (button_index < 10) {
            toolbar_row = 0; // Numbers row
        } else if (button_index < 20) {
            toolbar_row = 1; // QWERTY row
        } else if (button_index < 30) {
            toolbar_row = 2; // ASDF row
        } else if (button_index < 40) {
            toolbar_row = 3; // ZXCV row
        } else {
            toolbar_row = 4; // Special row
        }
        if (state->keyboard_bars[toolbar_row]) {
            ui_toolbar_draw(state->keyboard_bars[toolbar_row]);
        }
    }
}

// Create keyboard layout
static void create_keyboard_layout(osk_state_t *state) {
    int screen_cols = text_mode_get_cols();
    int screen_rows = text_mode_get_rows();

    // Calculate available space for keyboard
    int available_rows = screen_rows - state->keyboard_start_y;
    int total_keyboard_rows = 5; // 5 keyboard rows (4 main + 1 special)

    // Calculate button height to fit available space with vertical gaps
    int vertical_gap = 1; // 1-row gap between toolbars
    int total_gap_rows = (total_keyboard_rows - 1) * vertical_gap; // Total gap space
    
    // Calculate button height - distribute available space among toolbars
    int button_height = available_rows / total_keyboard_rows;
    if (button_height < 1) {
        button_height = 1; // Minimum height for touch
    }
    
    // If we have plenty of space, make buttons taller
    if (button_height == 1 && available_rows > total_keyboard_rows) {
        button_height = available_rows / total_keyboard_rows;
    }
    
    // Calculate total space needed by keyboard
    int total_keyboard_space = (total_keyboard_rows * button_height) + total_gap_rows;
    
    // Position keyboard so last toolbar is flush to bottom of screen
    int keyboard_start_y = screen_rows - total_keyboard_space;
    if (keyboard_start_y < state->keyboard_start_y) {
        keyboard_start_y = state->keyboard_start_y; // Ensure we don't overlap input area
        // Recalculate button height if we don't have enough space
        button_height = (available_rows - total_gap_rows) / total_keyboard_rows;
        if (button_height < 1) button_height = 1;
        // Recalculate total space with new button height
        total_keyboard_space = (total_keyboard_rows * button_height) + total_gap_rows;
        keyboard_start_y = screen_rows - total_keyboard_space;
    }

// Define keyboard rows using toolbars - remove duplicated buttons, let toolbars handle width
    const char *row1_labels[10] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
    const char *row2_labels[10] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"};
    const char *row3_labels[10] = {"a", "s", "d", "f", "g", "h", "j", "k", "l", ";"};
    const char *row4_labels[10] = {"z", "x", "c", "v", "b", "n", "m", ",", ".", "/"};
    const char *row5_labels[6] = {"\xE2\x9C\x98", "⇧", "#@", "⌫", "_", "⏎"};
    
    // Symbol keyboard layout
    const char *symbol_row1_labels[10] = {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"};
    const char *symbol_row2_labels[10] = {"-", "_", "=", "+", "[", "]", "{", "}", "|", "\\"};
    const char *symbol_row3_labels[10] = {":", ";", "'", "\"", "<", ">", ",", ".", "?", "/"};
    const char *symbol_row4_labels[10] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
    const char *symbol_row5_labels[6] = {"\xE2\x9C\x98", "⇧", "#@", "⌫", "_", "⏎"};

    int current_y = keyboard_start_y;
    int button_index = 0;

    // Create toolbar for each row with vertical gaps
    state->keyboard_bars[0] = ui_toolbar_create(current_y, button_height, 10, row1_labels);
    current_y += button_height + 1; // Add vertical gap after each row

    state->keyboard_bars[1] = ui_toolbar_create(current_y, button_height, 10, row2_labels);
    current_y += button_height + 1; // Add vertical gap after each row

    state->keyboard_bars[2] = ui_toolbar_create(current_y, button_height, 10, row3_labels);
    current_y += button_height + 1; // Add vertical gap after each row

    state->keyboard_bars[3] = ui_toolbar_create(current_y, button_height, 10, row4_labels);
    current_y += button_height + 1; // Add vertical gap after each row

    // Last row doesn't need gap after it
    state->keyboard_bars[4] = ui_toolbar_create(current_y, button_height, 6, row5_labels);

    // Store original labels for shift handling
    // Fix: Handle each row directly instead of using a jagged array
    for (int row = 0; row < 5; row++) {
        const char **current_row = NULL;
        int count = 0;
        
        // Get the current row and count
        switch (row) {
            case 0: current_row = row1_labels; count = 10; break;
            case 1: current_row = row2_labels; count = 10; break;
            case 2: current_row = row3_labels; count = 10; break;
            case 3: current_row = row4_labels; count = 10; break;
             case 4: current_row = row5_labels; count = 6; break;
        }
        
        for (int col = 0; col < count; col++) {
            // Special row handling - all keys are now unique
            if (row == 4) {
                size_t len = strlen(current_row[col]);
                state->original_labels[button_index] = (char*)malloc(len + 1);
                if (state->original_labels[button_index]) {
                    memcpy(state->original_labels[button_index], current_row[col], len + 1);
                }
                button_index++;
            } else {
                // Regular keyboard keys
                size_t len = strlen(current_row[col]);
                state->original_labels[button_index] = (char*)malloc(len + 1);
                if (state->original_labels[button_index]) {
                    memcpy(state->original_labels[button_index], current_row[col], len + 1);
                }
                button_index++;
            }
        }
    }

    state->total_button_count = button_index;

    // Initialize all button labels
    for (int i = 0; i < state->total_button_count; i++) {
        update_button_label(state, i);
    }

    // Draw all toolbars to ensure they're visible
    for (int i = 0; i < 5; i++) {
        if (state->keyboard_bars[i]) {
            ui_toolbar_draw(state->keyboard_bars[i]);
        }
    }
}

// Destroy keyboard layout
static void destroy_keyboard_layout(osk_state_t *state) {
    // Destroy toolbars and their buttons
    for (int i = 0; i < 5; i++) {
        if (state->keyboard_bars[i]) {
            ui_toolbar_destroy(state->keyboard_bars[i]);
            state->keyboard_bars[i] = NULL;
        }
    }

    // Free original labels
    for (int i = 0; i < state->total_button_count; i++) {
        if (state->original_labels[i]) {
            free(state->original_labels[i]);
            state->original_labels[i] = NULL;
        }
    }

    state->total_button_count = 0;
}

// Draw input display area
static void draw_input_display(osk_state_t *state, const char *title) {
    int screen_cols = text_mode_get_cols();

    int input_height = 2;

    // Draw title
    if (title) {
        text_mode_print_at_attr_bg(0, state->title_y, title, TEXT_COLOR_YELLOW, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD);
    }

    // Draw input box with extra spacing
    int input_x = 0;
    int input_width = state->input_display_width;
    int input_area_height = input_height + 1; // Add one row for spacing

    // Clear the input area (including extra spacing row)
    for (int y = state->input_y; y < state->input_y + input_area_height; y++) {
        for (int x = 0; x < input_width; x++) {
            text_mode_print_at(x, y, " ");
        }
    }

    // Draw bordered box (with gap before bottom border)
    for (int y = state->input_y; y < state->input_y + input_area_height; y++) {
        for (int x = input_x; x < input_x + input_width; x++) {
            uint8_t attr = TEXT_ATTR_NORMAL;

            if (y == state->input_y) {
                attr |= TEXT_ATTR_BORDER_TOP;
            }
            if (y == state->input_y + input_area_height - 1) {
                attr |= TEXT_ATTR_UNDERLINE; // Bottom border (one row lower)
            }
            if (x == input_x) {
                attr |= TEXT_ATTR_BORDER_LEFT;
            }
            if (x == input_x + input_width - 1) {
                attr |= TEXT_ATTR_BORDER_RIGHT;
            }

            text_mode_print_at_attr_bg(x, y, " ", TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, attr);
        }
    }

    // Display input text
    const char *display_text = state->mask_input ? "****************" : state->input_buffer;

    // Calculate text position
    int text_len = strlen(display_text);
    int max_display_width = input_width - 2;
    if (text_len > max_display_width) {
        display_text += text_len - max_display_width;
        text_len = max_display_width;
    }

    int text_x = input_x + 1 + (max_display_width - text_len) / 2;
    int text_y = state->input_y + input_height / 2;

    if (text_x >= input_x && text_y >= state->input_y) {
        // Draw the text character by character to add cursor
        for (int i = 0; i < text_len; i++) {
            int char_x = text_x + i;
            if (char_x < input_x + input_width - 1) {
                char str[2] = {display_text[i], '\0'};

                // Add cursor to current character
                if (i == state->cursor_pos) {
                    text_mode_print_at_attr_bg(char_x, text_y, str, TEXT_COLOR_YELLOW, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD | TEXT_ATTR_UNDERLINE);
                } else {
                    text_mode_print_at_attr_bg(char_x, text_y, str, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
                }
            }
        }

        // If cursor is at the end, show it there
        if (state->cursor_pos == text_len) {
            int cursor_x = text_x + state->cursor_pos;
            if (cursor_x < input_x + input_width - 1) {
                text_mode_print_at_attr_bg(cursor_x, text_y, "_", TEXT_COLOR_YELLOW, TEXT_COLOR_BLACK, TEXT_ATTR_BOLD);
            }
        }
    }

    text_mode_flush();
}

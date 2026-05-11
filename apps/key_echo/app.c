#include "os_core.h"
#include "hardware.h"
#include <stdio.h>
#include <string.h>

// Display position for echo
static int display_x = 0;
static int display_y = 0;
#define MAX_CHARS_PER_LINE 20
#define MAX_LINES 15

static char display_buffer[MAX_LINES][MAX_CHARS_PER_LINE + 1];
static int current_line = 0;
static int current_col = 0;

void app_init(app_context_t *ctx) {
    printf("🎹 KEY_ECHO APP_INIT called\n");
    printf("🎹 Initial subscriptions: 0x%lX\n", (unsigned long)ctx->subscriptions);

    // Initialize display buffer
    memset(display_buffer, 0, sizeof(display_buffer));

    // Subscribe to keyboard events
    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0; // No timer needed

    printf("🎹 Set subscriptions to 0x%lX (EVENT_KEYBOARD=0x%lX)\n",
           (unsigned long)ctx->subscriptions, (unsigned long)EVENT_KEYBOARD);

    printf("Key Echo app initialized\n");
    printf("Keyboard events will be echoed to screen and serial\n");

    // Display startup message
    display_x = 0;
    display_y = 0;
    current_line = 0;
    current_col = 0;

    // Show welcome message on display
    display_clear(0x0000); // Black background
    display_draw_text(5, 5, "Key Echo v1.0", 0x07E0); // Green
    display_draw_text(5, 25, "Type on keyboard:", 0xFFFF); // White

    display_x = 5;
    display_y = 45; // Start below the header

    printf("🎹 Display initialized, ready for keyboard input\n");
}

void app_checkpoint(app_context_t *ctx) {
    // Save display position
    checkpoint_save_int("display_x", display_x);
    checkpoint_save_int("display_y", display_y);
    checkpoint_save_int("current_line", current_line);
    checkpoint_save_int("current_col", current_col);

    // Save display buffer content
    for (int i = 0; i < MAX_LINES; i++) {
        char key[20];
        snprintf(key, sizeof(key), "line_%d", i);
        checkpoint_save_string(key, display_buffer[i]);
    }

    printf("Key Echo state saved\n");
}

void app_close(app_context_t *ctx) {
    printf("Key Echo app closing\n");
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type != EVENT_KEYBOARD) {
        return;
    }

    if (event->keyboard.pressed) {
        char key = event->keyboard.key;

        // Echo to serial
        printf("Key: %c (0x%02X)\n", key, (unsigned char)key);

        // Handle special keys
        if (key == '\n' || key == '\r') {
            // Enter key - move to next line
            current_line++;
            current_col = 0;
            display_y += 10; // Move down one line
            display_x = 5;   // Reset to left margin

            if (display_y > 300) {
                // Scroll - clear screen and restart
                display_clear(0x0000);
                display_draw_text(5, 5, "Key Echo v1.0", 0x07E0);
                display_draw_text(5, 25, "Type on keyboard:", 0xFFFF);
                display_y = 45;
                display_x = 5;
                current_line = 0;
            }
        } else if (key == '\b' || key == 127) {
            // Backspace - move back
            if (current_col > 0) {
                current_col--;
                display_x -= 6; // Approximate character width

                // Clear the character
                char str[2] = " ";
                display_draw_text(display_x, display_y, str, 0xFFFF);
            }
        } else if (key >= 32 && key <= 126) {
            // Printable character
            if (current_col < MAX_CHARS_PER_LINE) {
                // Display the character
                char str[2] = {key, '\0'};
                display_draw_text(display_x, display_y, str, 0xFFFF); // White

                // Store in buffer
                display_buffer[current_line][current_col] = key;
                current_col++;
                display_x += 6; // Approximate character width

                // Check for line wrap
                if (current_col >= MAX_CHARS_PER_LINE) {
                    current_line++;
                    current_col = 0;
                    display_y += 10;
                    display_x = 5;

                    if (display_y > 300) {
                        // Scroll
                        display_clear(0x0000);
                        display_draw_text(5, 5, "Key Echo v1.0", 0x07E0);
                        display_draw_text(5, 25, "Type on keyboard:", 0xFFFF);
                        display_y = 45;
                        display_x = 5;
                        current_line = 0;
                    }
                }
            }
        }
    }
}

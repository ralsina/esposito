#include "app_launcher.h"
#include "hardware.h"
#include "os_core.h"
#include "text_mode.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define IMAGE_VIEWER_MAX_PATH 192

typedef struct {
    char path[IMAGE_VIEWER_MAX_PATH];
    int image_width;
    int image_height;
    bool showing_image;
} image_viewer_state_t;

static const char *TAG = "image_viewer";
static image_viewer_state_t state;

static const char *image_viewer_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void image_viewer_show_message(const char *title, const char *line1, const char *line2, const char *line3) {
    text_mode_clear(TEXT_COLOR_BLACK);
    text_mode_print_at_attr(2, 1, title, TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    if (line1 && line1[0]) {
        text_mode_print_at_attr(2, 4, line1, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    }
    if (line2 && line2[0]) {
        text_mode_print_at_attr(2, 6, line2, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    }
    if (line3 && line3[0]) {
        text_mode_print_at_attr(2, 8, line3, TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
    }
}

static bool image_viewer_render_current(void) {
    int screen_width = text_mode_get_cols() * text_mode_get_char_width();
    int screen_height = text_mode_get_rows() * text_mode_get_char_height();

    if (!display_get_jpg_size(state.path, &state.image_width, &state.image_height)) {
        image_viewer_show_message(
            "Image Viewer",
            "Could not read JPEG header.",
            image_viewer_basename(state.path),
            "Open a valid .jpg or .jpeg file."
        );
        state.showing_image = false;
        return false;
    }

    if (state.image_width > screen_width * 8 || state.image_height > screen_height * 8) {
        char actual_line[64];
        char limit_line[64];
        snprintf(actual_line, sizeof(actual_line), "Image: %dx%d", state.image_width, state.image_height);
        snprintf(limit_line, sizeof(limit_line), "Limit: %dx%d", screen_width * 8, screen_height * 8);
        image_viewer_show_message("Image too large", actual_line, limit_line, image_viewer_basename(state.path));
        state.showing_image = false;
        return false;
    }

    display_clear(0x0000);
    if (!display_draw_jpg_fit(state.path, NULL, NULL)) {
        image_viewer_show_message(
            "Image Viewer",
            "JPEG decode failed.",
            image_viewer_basename(state.path),
            "Press q or Esc to return."
        );
        state.showing_image = false;
        return false;
    }

    state.showing_image = true;
    return true;
}

void app_init(app_context_t *ctx) {
    memset(&state, 0, sizeof(state));

    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;

    text_mode_init();

    size_t startup_len = os_consume_startup_file(state.path, sizeof(state.path));
    if (startup_len == 0 || state.path[0] == '\0') {
        image_viewer_show_message(
            "Image Viewer",
            "No image path provided.",
            "Open a .jpg from file manager.",
            "Press q or Esc to return."
        );
        os_log(TAG, "No startup image provided");
        return;
    }

    struct stat st;
    if (stat(state.path, &st) != 0 || !S_ISREG(st.st_mode)) {
        image_viewer_show_message(
            "Image Viewer",
            "File is missing or not regular.",
            image_viewer_basename(state.path),
            "Press q or Esc to return."
        );
        os_log(TAG, "Invalid startup path: %s", state.path);
        return;
    }

    image_viewer_render_current();
    os_log(TAG, "Opened image: %s", state.path);
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    display_clear(0x0000);
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;

    if (event->type != EVENT_KEYBOARD || !event->keyboard.pressed) {
        return;
    }

    if (event->keyboard.key == 27 || event->keyboard.key == 'q' || event->keyboard.key == 'Q') {
        app_launcher_start();
        return;
    }

    if (event->keyboard.key == 'r' || event->keyboard.key == 'R') {
        image_viewer_render_current();
    }
}

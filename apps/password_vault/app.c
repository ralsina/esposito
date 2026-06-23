#include "os_core.h"
#include "text_mode.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "password_vault";

#define MAX_PATH 256
#define MAX_FILES 50
#define MAX_NAME_LEN 64

typedef struct {
    char name[MAX_NAME_LEN];
    char full_path[MAX_PATH];
    bool is_dir;
    bool is_gpg_file;
} file_entry_t;

typedef struct {
    file_entry_t files[MAX_FILES];
    int file_count;
    int selected_index;
    char current_path[MAX_PATH];
    bool needs_redraw;
    char error_msg[256];
    bool has_error;
} app_state_t;

static app_state_t state;

static bool is_gpg_file(const char *name) {
    size_t len = strlen(name);
    return len > 4 && strcmp(&name[len - 4], ".gpg") == 0;
}

static void strip_gpg_extension(char *dest, const char *src, size_t dest_size) {
    size_t len = strlen(src);
    if (len > 4 && is_gpg_file(src)) {
        strncpy(dest, src, dest_size - 1);
        dest[len - 4] = '\0';  // Remove .gpg
    } else {
        strncpy(dest, src, dest_size - 1);
        dest[dest_size - 1] = '\0';
    }
}

static void scan_directory(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        snprintf(state.error_msg, sizeof(state.error_msg), "Cannot open: %s", path);
        state.has_error = true;
        return;
    }

    state.has_error = false;
    state.file_count = 0;
    state.selected_index = 0;
    strncpy(state.current_path, path, sizeof(state.current_path) - 1);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && state.file_count < MAX_FILES) {
        if (strcmp(entry->d_name, ".") == 0) continue;

        file_entry_t *file = &state.files[state.file_count];
        strncpy(file->name, entry->d_name, MAX_NAME_LEN - 1);
        file->name[MAX_NAME_LEN - 1] = '\0';

        snprintf(file->full_path, sizeof(file->full_path), "%s/%s", path, entry->d_name);

        // Check if it's a directory
        struct stat st;
        if (stat(file->full_path, &st) == 0) {
            file->is_dir = S_ISDIR(st.st_mode);
            file->is_gpg_file = !file->is_dir && is_gpg_file(entry->d_name);
        } else {
            file->is_dir = false;
            file->is_gpg_file = is_gpg_file(entry->d_name);
        }

        // Sort directories first, then .gpg files, then others
        if (strcmp(entry->d_name, "..") == 0) {
            // Keep .. at the top
        } else if (file->is_dir) {
            // Directories come after ..
        } else if (file->is_gpg_file) {
            // .gpg files come after directories
        } else {
            // Other files come last
        }

        state.file_count++;
    }
    closedir(dir);

    // Sort: directories first, then .gpg files
    for (int i = 0; i < state.file_count - 1; i++) {
        for (int j = i + 1; j < state.file_count; j++) {
            bool i_is_parent = strcmp(state.files[i].name, "..") == 0;
            bool j_is_parent = strcmp(state.files[j].name, "..") == 0;

            if (j_is_parent) {
                file_entry_t temp = state.files[i];
                state.files[i] = state.files[j];
                state.files[j] = temp;
            } else if (!i_is_parent && state.files[j].is_dir && !state.files[i].is_dir) {
                file_entry_t temp = state.files[i];
                state.files[i] = state.files[j];
                state.files[j] = temp;
            } else if (state.files[i].is_dir == state.files[j].is_dir) {
                if (state.files[j].is_gpg_file && !state.files[i].is_gpg_file) {
                    file_entry_t temp = state.files[i];
                    state.files[i] = state.files[j];
                    state.files[j] = temp;
                }
            }
        }
    }

    state.needs_redraw = true;
    os_log(TAG, "Scanned %s: %d files", path, state.file_count);
}

static void render_ui() {
    if (!state.needs_redraw) return;

    text_mode_clear(TEXT_COLOR_BLACK);

    // Title
    text_mode_print_at_attr(2, 1, "Password Vault", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);

    // Path
    const char *path_start = state.current_path;
    if (strlen(state.current_path) > 38) {
        path_start = &state.current_path[strlen(state.current_path) - 38];
    }
    text_mode_printf_at_attr(2, 2, TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_NORMAL, "Path: %s", path_start);

    // Draw separator
    text_mode_print_at_attr(1, 3, "─────────────────────────────────────────────────────────",
                            TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL);

    // File list
    int max_files = 18;
    int start_idx = 0;
    if (state.selected_index >= max_files) {
        start_idx = state.selected_index - max_files + 1;
    }

    for (int i = start_idx; i < state.file_count && i < start_idx + max_files; i++) {
        const file_entry_t *file = &state.files[i];
        int row = 4 + (i - start_idx);

        char display_name[MAX_NAME_LEN + 2];
        if (file->is_dir) {
            snprintf(display_name, sizeof(display_name), "[%s]", file->name);
        } else {
            strip_gpg_extension(display_name, file->name, sizeof(display_name));
        }

        uint8_t color = i == state.selected_index ? TEXT_COLOR_BRIGHT_YELLOW :
                       (file->is_gpg_file ? TEXT_COLOR_BRIGHT_GREEN :
                        file->is_dir ? TEXT_COLOR_BRIGHT_CYAN : TEXT_COLOR_WHITE);

        uint8_t attr = i == state.selected_index ? TEXT_ATTR_BOLD : TEXT_ATTR_NORMAL;
        text_mode_print_at_attr(2, row, display_name, color, attr);
    }

    // Error message
    if (state.has_error) {
        text_mode_print_at_attr(2, 23, state.error_msg, TEXT_COLOR_BRIGHT_RED, TEXT_ATTR_BOLD);
    }

    // Instructions
    text_mode_print_at_attr(2, 23, "W/S:Nav  Enter:Open  Esc:Up  Ctrl+Q:Quit",
                            TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL);

    state.needs_redraw = false;
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD;
    memset(&state, 0, sizeof(state));

    // Start at password store root
    scan_directory("/sdcard/password-store");

    os_log(TAG, "Password Vault initialized");
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;

        switch (key) {
            case 'w':  // Up
            case 'W':
                if (state.selected_index > 0) {
                    state.selected_index--;
                    state.needs_redraw = true;
                }
                break;

            case 's':  // Down
            case 'S':
                if (state.selected_index < state.file_count - 1) {
                    state.selected_index++;
                    state.needs_redraw = true;
                }
                break;

            case '\n':  // Enter - open file/directory
            case '\r':
                if (state.file_count > 0) {
                    const file_entry_t *file = &state.files[state.selected_index];
                    if (file->is_dir) {
                        scan_directory(file->full_path);
                    } else if (file->is_gpg_file) {
                        // TODO: Implement GPG decryption
                        snprintf(state.error_msg, sizeof(state.error_msg),
                                "GPG decryption not implemented yet: %s", file->name);
                        state.has_error = true;
                        state.needs_redraw = true;
                    }
                }
                break;

            case 27:  // ESC - go up
                if (strcmp(state.current_path, "/sdcard/password-store") != 0) {
                    char parent_path[MAX_PATH];
                    strncpy(parent_path, state.current_path, sizeof(parent_path) - 1);
                    char *last_slash = strrchr(parent_path, '/');
                    if (last_slash && last_slash != parent_path) {
                        *last_slash = '\0';
                        scan_directory(parent_path);
                    }
                }
                break;

            case 'Q':  // Ctrl+Q - quit
                if (event->keyboard.modifiers & MODIFIER_CTRL) {
                    os_load_app("launcher");
                    return;
                }
                break;
        }

        render_ui();
    }
}

void app_checkpoint(app_context_t *ctx) {
    config_set_string("last_path", state.current_path);
    os_log(TAG, "checkpoint: %s", state.current_path);
}

void app_close(app_context_t *ctx) {
    text_mode_clear(TEXT_COLOR_BLACK);
    os_log(TAG, "closed");
}

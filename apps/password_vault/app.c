#include "os_core.h"
#include "text_mode.h"
#include "app_config.h"
#include "app_heap.h"
#include "key_storage.h"
#include "openpgp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>

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

typedef enum {
    VAULT_STATE_LOCKED,       /* Waiting for master password */
    VAULT_STATE_SETUP,        /* No key exists, show setup prompt */
    VAULT_STATE_UNLOCKED,     /* Key loaded, browsing files */
    VAULT_STATE_PASSWORD_VIEW /* Viewing decrypted password */
} vault_state_t;

typedef struct {
    file_entry_t files[MAX_FILES];
    int file_count;
    int selected_index;
    char current_path[MAX_PATH];
    bool needs_redraw;
    char error_msg[256];
    bool has_error;

    /* Vault state */
    vault_state_t vault_state;
    char master_password[64];
    int master_password_len;
    int failed_attempts;

    /* Password view state */
    char current_password[256];
    bool password_revealed;
    int64_t password_reveal_time;
    int64_t last_activity;    /* For auto-wipe timer (5 min inactivity) */

    /* Loaded private key (PEM format) - allocated with app_malloc */
    char *private_key_pem;
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

/* ── Vault state helpers ── */

static void wipe_vault_state(void) {
    /* Clear sensitive data from memory */
    if (state.private_key_pem) {
        /* Zero out the key before freeing */
        size_t len = strlen(state.private_key_pem);
        memset(state.private_key_pem, 0, len);
        app_free(state.private_key_pem);
        state.private_key_pem = NULL;
    }

    memset(state.master_password, 0, sizeof(state.master_password));
    state.master_password_len = 0;
    state.failed_attempts = 0;

    memset(state.current_password, 0, sizeof(state.current_password));
    state.password_revealed = false;
    state.password_reveal_time = 0;

    state.vault_state = VAULT_STATE_LOCKED;
}

static void update_last_activity(void) {
    state.last_activity = esp_timer_get_time() / 1000; /* Convert to milliseconds */
}

static bool check_auto_wipe(void) {
    if (state.vault_state != VAULT_STATE_UNLOCKED &&
        state.vault_state != VAULT_STATE_PASSWORD_VIEW) {
        return false;
    }

    int64_t now = esp_timer_get_time() / 1000;
    int64_t elapsed = now - state.last_activity;

    /* Auto-wipe after 5 minutes (300000 ms) of inactivity */
    if (elapsed > 300000) {
        ESP_LOGI(TAG, "Auto-wiping vault after %lld ms of inactivity", elapsed);
        wipe_vault_state();
        state.needs_redraw = true;
        return true;
    }

    return false;
}

/* ── UI Screens ── */

static void render_password_prompt(void) {
    text_mode_clear(TEXT_COLOR_BLACK);

    text_mode_print_at_attr(2, 8, "═════════════════════════════════════════",
                            TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(2, 9, "  Password Vault", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(2, 10, "═════════════════════════════════════════",
                            TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);

    /* Prompt for password */
    if (state.failed_attempts > 0) {
        text_mode_printf_at_color(2, 14, TEXT_COLOR_BRIGHT_RED,
                                 "Wrong password (%d attempts left)",
                                 5 - state.failed_attempts);
    }

    text_mode_printf_at_attr(2, 16, TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_NORMAL,
                             "Master password: %s",
                             state.master_password);

    /* Show masked characters */
    int mask_len = state.master_password_len;
    if (mask_len > 0) {
        char mask[64];
        memset(mask, '*', mask_len);
        mask[mask_len] = '\0';
        text_mode_print_at_attr(17 + strlen("Master password: "), 16,
                                mask, TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
    }

    text_mode_print_at_attr(2, 20, "Enter password, Ctrl+C to cancel",
                            TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL);
}

static void render_setup_prompt(void) {
    text_mode_clear(TEXT_COLOR_BLACK);

    text_mode_print_at_attr(2, 8, "═════════════════════════════════════════",
                            TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(2, 9, "  Password Vault - Setup Required", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(2, 10, "═════════════════════════════════════════",
                            TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);

    text_mode_print_at_attr(2, 13, "No encryption key found.", TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(2, 15, "To set up the password vault:", TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(2, 17, "1. Export your GPG key:", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(2, 18, "   gpg --export-secret-keys KEY_ID > key.gpg", TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(2, 20, "2. Convert to PEM using the setup tool:", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(2, 21, "   tools/setup_password_vault.py key.gpg > key.pem", TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(2, 23, "3. Copy key.pem to SD card:", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_NORMAL);
    text_mode_print_at_attr(2, 24, "   /sdcard/apps/password_vault/config/private_key.pem", TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL);

    text_mode_print_at_attr(2, 26, "Press any key to return to launcher", TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL);
}

static void render_password_view(void) {
    text_mode_clear(TEXT_COLOR_BLACK);

    text_mode_print_at_attr(2, 1, "═════════════════════════════════════════",
                            TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(2, 2, "  Password Revealed", TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(2, 3, "═════════════════════════════════════════",
                            TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);

    text_mode_print_at_attr(2, 7, "Password:", TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_NORMAL);

    if (state.password_revealed) {
        /* Show password */
        text_mode_print_at_attr(2, 9, state.current_password,
                                TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_BOLD);
    } else {
        /* Show asterisks */
        text_mode_print_at_attr(2, 9, "******** (Press Enter to reveal)",
                                TEXT_COLOR_BRIGHT_YELLOW, TEXT_ATTR_NORMAL);
    }

    /* Auto-hide countdown */
    int64_t now = esp_timer_get_time() / 1000;
    int64_t remaining = 30000 - (now - state.password_reveal_time);
    if (remaining < 0) remaining = 0;

    text_mode_printf_at_attr(2, 15, TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL,
                             "Auto-hide in: %lld seconds", remaining / 1000);

    text_mode_print_at_attr(2, 20, "Enter: Toggle reveal  Esc: Back  Ctrl+Q: Quit",
                            TEXT_COLOR_BRIGHT_BLACK, TEXT_ATTR_NORMAL);
}

static void render_ui() {
    if (!state.needs_redraw) return;

    /* Check auto-wipe on every render */
    if (check_auto_wipe()) {
        /* State was wiped, fall through to render appropriate screen */
    }

    /* Render based on vault state */
    switch (state.vault_state) {
        case VAULT_STATE_LOCKED:
            render_password_prompt();
            state.needs_redraw = false;
            return;

        case VAULT_STATE_SETUP:
            render_setup_prompt();
            state.needs_redraw = false;
            return;

        case VAULT_STATE_PASSWORD_VIEW:
            render_password_view();
            state.needs_redraw = false;
            return;

        case VAULT_STATE_UNLOCKED:
            /* Continue to file browser UI below */
            break;
    }

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

    /* Initialize activity timer */
    state.last_activity = esp_timer_get_time() / 1000;

    /* Check if encryption key exists */
    if (key_storage_exists()) {
        state.vault_state = VAULT_STATE_LOCKED;
        ESP_LOGI(TAG, "Vault locked - master password required");
    } else {
        state.vault_state = VAULT_STATE_SETUP;
        ESP_LOGI(TAG, "No encryption key found - setup required");
    }

    /* Initialize file browser for password store */
    scan_directory("/sdcard/password-store");

    state.needs_redraw = true;
    os_log(TAG, "Password Vault initialized");
}

void app_event(app_context_t *ctx, event_t *event) {
    if (event->type == EVENT_KEYBOARD && event->keyboard.pressed) {
        char key = event->keyboard.key;

        /* Handle Ctrl+C to cancel password entry */
        if (key == 'C' && (event->keyboard.modifiers & MODIFIER_CTRL)) {
            if (state.vault_state == VAULT_STATE_LOCKED) {
                /* Cancel and return to launcher */
                os_load_app("launcher");
                return;
            }
        }

        /* Update activity on any key press */
        update_last_activity();

        switch (state.vault_state) {
            case VAULT_STATE_SETUP:
                /* Any key returns to launcher */
                os_load_app("launcher");
                return;

            case VAULT_STATE_LOCKED:
                /* Password input mode */
                if (key == '\n' || key == '\r') {
                    /* Enter - attempt unlock */
                    if (state.master_password_len > 0) {
                        state.master_password[state.master_password_len] = '\0';

                        /* Load and decrypt private key */
                        char pem_buf[4096];
                        if (key_storage_load(pem_buf, sizeof(pem_buf), state.master_password)) {
                            /* Success - allocate and store key */
                            size_t pem_len = strlen(pem_buf);
                            state.private_key_pem = (char *)app_malloc(pem_len + 1);
                            if (state.private_key_pem) {
                                strcpy(state.private_key_pem, pem_buf);

                                /* Clear password from memory */
                                memset(state.master_password, 0, sizeof(state.master_password));
                                state.master_password_len = 0;

                                state.vault_state = VAULT_STATE_UNLOCKED;
                                state.failed_attempts = 0;
                                state.needs_redraw = true;

                                ESP_LOGI(TAG, "Vault unlocked successfully");
                                os_log(TAG, "Vault unlocked");
                            } else {
                                ESP_LOGE(TAG, "Failed to allocate memory for private key");
                                snprintf(state.error_msg, sizeof(state.error_msg),
                                        "Memory allocation failed");
                                state.has_error = true;
                                state.needs_redraw = true;
                            }
                        } else {
                            /* Wrong password */
                            state.failed_attempts++;
                            memset(state.master_password, 0, sizeof(state.master_password));
                            state.master_password_len = 0;

                            if (state.failed_attempts >= 5) {
                                ESP_LOGW(TAG, "Too many failed attempts - returning to launcher");
                                os_load_app("launcher");
                                return;
                            }

                            state.needs_redraw = true;
                            ESP_LOGW(TAG, "Wrong password (attempt %d)", state.failed_attempts);
                        }
                    }
                } else if (key == 127 || key == 8) {
                    /* Backspace/Delete */
                    if (state.master_password_len > 0) {
                        state.master_password_len--;
                        state.master_password[state.master_password_len] = '\0';
                        state.needs_redraw = true;
                    }
                } else if (key >= 32 && key < 127 && state.master_password_len < 63) {
                    /* Regular character - add to password */
                    state.master_password[state.master_password_len++] = key;
                    state.needs_redraw = true;
                }
                break;

            case VAULT_STATE_PASSWORD_VIEW:
                /* Password view mode */
                if (key == '\n' || key == '\r') {
                    /* Enter - toggle reveal */
                    state.password_revealed = !state.password_revealed;
                    state.password_reveal_time = esp_timer_get_time() / 1000;
                    state.needs_redraw = true;
                } else if (key == 27) {
                    /* ESC - return to browser */
                    state.vault_state = VAULT_STATE_UNLOCKED;
                    memset(state.current_password, 0, sizeof(state.current_password));
                    state.password_revealed = false;
                    state.needs_redraw = true;
                } else if (key == 'Q' && (event->keyboard.modifiers & MODIFIER_CTRL)) {
                    /* Ctrl+Q - quit */
                    wipe_vault_state();
                    os_load_app("launcher");
                    return;
                }
                break;

            case VAULT_STATE_UNLOCKED:
                /* Normal file browser mode */
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
                                /* Attempt to decrypt GPG file */
                                ESP_LOGI(TAG, "Decrypting: %s", file->full_path);

                                pgp_file_t pgp_file;
                                if (pgp_parse_file(file->full_path, &pgp_file)) {
                                    /* Use loaded private key to decrypt */
                                    if (pgp_decrypt_rsa(&pgp_file,
                                                        (const uint8_t *)state.private_key_pem,
                                                        strlen(state.private_key_pem))) {
                                        if (pgp_decrypt_aes(&pgp_file,
                                                           pgp_file.session_key,
                                                           pgp_file.session_key_len)) {
                                            /* Extract password */
                                            size_t pw_len = pgp_file.literal_data_len;
                                            if (pw_len > 0 && pw_len < sizeof(state.current_password)) {
                                                memcpy(state.current_password,
                                                       pgp_file.literal_data, pw_len);
                                                state.current_password[pw_len] = '\0';

                                                state.vault_state = VAULT_STATE_PASSWORD_VIEW;
                                                state.password_revealed = false;
                                                state.password_reveal_time = esp_timer_get_time() / 1000;
                                                state.needs_redraw = true;

                                                ESP_LOGI(TAG, "Password decrypted successfully");
                                            } else {
                                                snprintf(state.error_msg, sizeof(state.error_msg),
                                                        "Invalid password data");
                                                state.has_error = true;
                                                state.needs_redraw = true;
                                            }
                                        } else {
                                            snprintf(state.error_msg, sizeof(state.error_msg),
                                                    "AES decryption failed");
                                            state.has_error = true;
                                            state.needs_redraw = true;
                                        }
                                    } else {
                                        snprintf(state.error_msg, sizeof(state.error_msg),
                                                "RSA decryption failed");
                                        state.has_error = true;
                                        state.needs_redraw = true;
                                    }
                                    pgp_free_file(&pgp_file);
                                } else {
                                    snprintf(state.error_msg, sizeof(state.error_msg),
                                            "Failed to parse GPG file");
                                    state.has_error = true;
                                    state.needs_redraw = true;
                                }
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
                            wipe_vault_state();
                            os_load_app("launcher");
                            return;
                        }
                        break;
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
    wipe_vault_state();
    text_mode_clear(TEXT_COLOR_BLACK);
    os_log(TAG, "closed");
}

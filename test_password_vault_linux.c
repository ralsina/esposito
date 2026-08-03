#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <termios.h>
#include <sys/select.h>

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
static struct termios orig_termios;

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
    printf("Scanned %s: %d files\n", path, state.file_count);
}

static void render_ui() {
    if (!state.needs_redraw) return;

    system("clear"); // Clear screen

    // Title
    printf("═════════════════════════════════════════\n");
    printf("  Password Vault (POC)\n");
    printf("═════════════════════════════════════════\n");

    // Path
    const char *path_start = state.current_path;
    if (strlen(state.current_path) > 50) {
        path_start = &state.current_path[strlen(state.current_path) - 50];
    }
    printf("Path: %s\n", path_start);
    printf("─────────────────────────────────────────\n");

    // File list
    int max_files = 20;
    int start_idx = 0;
    if (state.selected_index >= max_files) {
        start_idx = state.selected_index - max_files + 1;
    }

    for (int i = start_idx; i < state.file_count && i < start_idx + max_files; i++) {
        const file_entry_t *file = &state.files[i];
        char display_name[MAX_NAME_LEN + 2];
        if (file->is_dir) {
            snprintf(display_name, sizeof(display_name), "[%s]", file->name);
        } else {
            strip_gpg_extension(display_name, file->name, sizeof(display_name));
        }

        char marker = (i == state.selected_index) ? '>' : ' ';
        char type_marker = file->is_gpg_file ? '*' : ' ';

        printf("%c %c %s\n", marker, type_marker, display_name);
    }

    printf("─────────────────────────────────────────\n");
    if (state.has_error) {
        printf("ERROR: %s\n", state.error_msg);
    } else {
        printf("Commands: w/s=nav, Enter=open, Esc=up, q=quit\n");
    }

    state.needs_redraw = false;
}

static void enable_raw_mode() {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    orig_termios = term;
    term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

static void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static int kbhit() {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
}

static int getch() {
    int ch;
    read(STDIN_FILENO, &ch, 1);
    return ch;
}

static void show_file_info(const file_entry_t *file) {
    system("clear");
    printf("═════════════════════════════════════════\n");
    printf("  File Information\n");
    printf("═════════════════════════════════════════\n");
    printf("Name: %s\n", file->name);
    printf("Path: %s\n", file->full_path);
    printf("Type: %s\n", file->is_dir ? "Directory" : (file->is_gpg_file ? "GPG Encrypted File" : "Regular File"));

    if (file->is_gpg_file) {
        printf("Status: GPG DECRYPTION NOT IMPLEMENTED YET\n");
        printf("\nThis is where we would:\n");
        printf("1. Parse OpenPGP packets\n");
        printf("2. Decrypt RSA session key\n");
        printf("3. Decrypt AES password data\n");
        printf("4. Display the password\n");
    }

    printf("\nPress any key to continue...\n");
    getch();
    state.needs_redraw = true;
}

int main() {
    memset(&state, 0, sizeof(state));

    // Start at test password store
    const char *test_path = getenv("TEST_PASSWORD_STORE");
    if (!test_path) {
        test_path = "/home/ralsina/test_password_store";
    }

    printf("Starting Password Vault POC...\n");
    printf("Using path: %s\n", test_path);
    printf("Press any key to continue...\n");
    getch();

    scan_directory(test_path);
    enable_raw_mode();

    while (1) {
        render_ui();

        if (kbhit()) {
            int key = getch();

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
                            show_file_info(file);
                        }
                    }
                    break;

                case 27:  // ESC - go up
                    {
                        // Check if it's a real escape (no following characters)
                        usleep(10000); // 10ms delay to check for escape sequence
                        if (!kbhit()) {
                            // Single ESC key
                            if (strcmp(state.current_path, test_path) != 0) {
                                char parent_path[MAX_PATH];
                                strncpy(parent_path, state.current_path, sizeof(parent_path) - 1);
                                char *last_slash = strrchr(parent_path, '/');
                                if (last_slash && last_slash != parent_path) {
                                    *last_slash = '\0';
                                    scan_directory(parent_path);
                                }
                            }
                        } else {
                            // Consume the escape sequence
                            while (kbhit()) getch();
                        }
                    }
                    break;

                case 'q':  // Quit
                case 'Q':
                    disable_raw_mode();
                    system("clear");
                    printf("Exiting Password Vault POC...\n");
                    return 0;
            }
        }

        usleep(10000); // 10ms sleep to prevent CPU spinning
    }

    disable_raw_mode();
    return 0;
}

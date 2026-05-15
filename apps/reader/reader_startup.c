#include "reader_startup.h"

#include "app_config.h"
#include "reader_events.h"
#include "os_core.h"
#include "text_mode.h"

#include <string.h>
#include <sys/stat.h>

void reader_startup_init(reader_state_t *state, int *bold_pending, int *underline_pending) {
    text_mode_init();
    state->search_buf[0] = '\0';
    state->search_status[0] = '\0';

    char saved_file[MAX_PATH];
    size_t saved_len = os_consume_startup_file(saved_file, sizeof(saved_file));
    // os_consume_startup_file binds and then unbinds globally; restore reader binding
    // before using config_get_string fallbacks.
    config_bind_app("reader");
    if (saved_len == 0) {
        saved_len = config_get_string(KEY_LAST_FILE, "", saved_file, sizeof(saved_file));
    }
    if (saved_len == 0) {
        saved_len = config_get_string(KEY_LEGACY_LAST_FILE, "", saved_file, sizeof(saved_file));
    }

    if (saved_len > 0 && saved_file[0]) {
        struct stat st;
        if (stat(saved_file, &st) == 0 && S_ISREG(st.st_mode)) {
            if (reader_events_open_book(state, saved_file, bold_pending, underline_pending)) {
                reader_events_enter_reading_mode(state, bold_pending, underline_pending);
                return;
            }
        }
    }

    reader_events_show_file_list(state);
}

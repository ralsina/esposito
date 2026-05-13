#include "reader_startup.h"

#include "checkpoint.h"
#include "reader_events.h"
#include "text_mode.h"

#include <string.h>
#include <sys/stat.h>

void reader_startup_init(reader_state_t *state, int *bold_pending, int *underline_pending) {
    text_mode_init();
    checkpoint_open("reader");

    const char *saved_file = checkpoint_load_string(KEY_LAST_FILE);
    if (!saved_file || !saved_file[0]) {
        saved_file = checkpoint_load_string(KEY_LEGACY_LAST_FILE);
    }

    if (saved_file && saved_file[0]) {
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

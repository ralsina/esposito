#include "ui2.h"
#include <stdio.h>

static ui2_screen_t *screen;
static ui2_list_t *list;
static ui2_text_t *info;
static ui2_label_t *status;
static char status_buf[40];

static const char *fruits[] = {"Apple", "Banana", "Cherry", "Date", "Elderberry", "Fig", "Grape"};

static const char *descriptions[] = {
    "Crisp and sweet,\nperfect for pies\nor just snacking.",
    "Rich in potassium,\ngreat in smoothies\nor sliced on oatmeal.",
    "Deep red or black,\nsweet and sometimes\ntart. Great in pies.",
    "Sweet and chewy,\ngrown on palm trees.\nA natural sweetener.",
    "Tart and tangy,\npacked with\nantioxidants.",
    "Soft and sweet,\nwith a unique\ntexture and crunch.",
    "Sweet and juicy,\nperfect for wine,\nsnacks, or jelly."
};

static void show_description(int index) {
    ui2_text_clear(info);
    int y = 0;
    int line = 0;
    const char *d = descriptions[index];
    char buf[64];
    int bi = 0;
    for (const char *p = d; ; p++) {
        if (*p == '\n' || *p == '\0') {
            buf[bi] = '\0';
            if (bi > 0)
                ui2_text_print_at(info, 0, line++, buf,
                                  TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, TEXT_ATTR_NORMAL);
            else
                line++;
            bi = 0;
            if (*p == '\0') break;
        } else {
            if (bi < (int)sizeof(buf) - 1) buf[bi++] = *p;
        }
    }
}

static void on_fruit_activated(int index, void *data) {
    (void)data;
    snprintf(status_buf, sizeof(status_buf), "Selected: %s", fruits[index]);
    ui2_label_set_text(status, status_buf);
    show_description(index);
    ui2_screen_render(screen);
}

static void on_quit_clicked(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    os_load_app("launcher");
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH;
    text_mode_init();
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    screen = ui2_screen_create();
    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_ABSOLUTE);
    ui2_screen_set_root(screen, root);

    ui2_label_t *title = ui2_label_create(cols / 2 - 5, 0, "UI2 Example",
                                          TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    ui2_layout_add(root, UI2_WIDGET(title));

    list = ui2_list_create(2, 2, 28, rows - 5);
    ui2_list_set_items(list, fruits, 7);
    ui2_list_set_callbacks(list, NULL, on_fruit_activated, NULL);
    ui2_layout_add(root, UI2_WIDGET(list));

    info = ui2_text_create(32, 2, cols - 34, rows - 5);
    ui2_text_set_border(info, true, TEXT_COLOR_CYAN);
    ui2_layout_add(root, UI2_WIDGET(info));

    status_buf[0] = '\0';
    status = ui2_label_create(2, rows - 2, status_buf,
                              TEXT_COLOR_BRIGHT_GREEN, TEXT_ATTR_NORMAL);
    ui2_layout_add(root, UI2_WIDGET(status));

    ui2_button_t *quit = ui2_button_create(cols - 14, rows - 2, 12, 2, "Quit");
    ui2_button_set_callback(quit, on_quit_clicked, NULL);
    ui2_layout_add(root, UI2_WIDGET(quit));

    show_description(0);
    ui2_screen_focus_set(screen, UI2_WIDGET(list));
    ui2_screen_render(screen);
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;
    if (ui2_screen_handle_event(screen, event))
        ui2_screen_render(screen);
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
}

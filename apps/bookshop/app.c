#include "os_core.h"
#include "app_config.h"
#include "text_mode.h"
#include "core_json.h"
#include "ui2.h"
#include "lucide_icons.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#define CATALOG_URL  "https://esposito.ralsina.me/books/metadata.json"
#define CATALOG_PATH "/sdcard/.bookshop_cat.json"
#define BOOKS_BASE   "https://esposito.ralsina.me/books/"
#define SD_BOOKS_DIR "/sdcard/books"

#define MAX_BOOKS    100
#define DISPLAY_LEN  64

#define DOWNLOAD_RESULT_KEY "os/download_result"
#define DOWNLOAD_PATH_KEY   "os/download_path"
#define CATALOG_CACHE_HOURS 24

typedef struct {
    char title[80];
    char author[60];
    char markdown_file[128];
    int size;
} book_entry_t;

typedef enum {
    MODE_CATALOG,
    MODE_DOWNLOADING,
    MODE_ERROR,
} app_mode_t;

static ui2_screen_t *screen;
static ui2_list_t *book_list;
static book_entry_t books[MAX_BOOKS];
static int book_count;
static char *list_items[MAX_BOOKS];
static char list_texts[MAX_BOOKS][DISPLAY_LEN];
static uint8_t row_attrs[MAX_BOOKS];
static app_mode_t mode;
static char error_msg[64];
static int ignore_events;
static char download_sd_path[320];
static char download_url[768];
static char download_encoded[512];

static void on_book_activated(int index, void *data);
static void on_up_click(ui2_button_t *btn, void *data);
static void on_down_click(ui2_button_t *btn, void *data);
static void on_download_click(ui2_button_t *btn, void *data);
static void on_exit_click(ui2_button_t *btn, void *data);
static void rebuild_catalog_ui(void);
static void show_pending_download_result(void);
static bool is_catalog_fresh(void);
static int load_catalog_from_disk(void);

static void url_encode(const char *src, char *dst, size_t dst_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t di = 0;
    while (*src && di < dst_size - 1) {
        unsigned char c = (unsigned char)*src;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            dst[di++] = c;
        } else {
            if (di + 3 >= dst_size) break;
            dst[di++] = '%';
            dst[di++] = hex[c >> 4];
            dst[di++] = hex[c & 0xf];
        }
        src++;
    }
    dst[di] = '\0';
}

static int extract_string(const char *val, size_t vallen, char *out, size_t out_size) {
    if (!val || !out || out_size == 0) return 0;
    int start = 0;
    int end = (int)vallen;
    if (val[0] == '"') start = 1;
    if (end > 0 && val[end - 1] == '"') end--;
    int len = end - start;
    if (len < 0) len = 0;
    if ((size_t)len >= out_size) len = (int)out_size - 1;
    memcpy(out, val + start, len);
    out[len] = '\0';
    return len;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool is_catalog_fresh(void) {
    struct stat st;
    if (stat(CATALOG_PATH, &st) != 0) {
        return false;
    }
    time_t now = time(NULL);
    time_t cache_age_sec = now - st.st_mtime;
    time_t cache_limit_sec = CATALOG_CACHE_HOURS * 3600;
    return cache_age_sec < cache_limit_sec;
}

static int compare_books(const void *a, const void *b) {
    const book_entry_t *book_a = (const book_entry_t *)a;
    const book_entry_t *book_b = (const book_entry_t *)b;
    return strcmp(book_a->title, book_b->title);
}

static int load_catalog_from_disk(void) {
    struct stat st;
    if (stat(CATALOG_PATH, &st) != 0 || st.st_size <= 0) {
        printf("[bookshop] cannot stat catalog file\n");
        return -1;
    }

    size_t file_size = (size_t)st.st_size;
    char *catalog_buf = malloc(file_size + 1);
    if (!catalog_buf) {
        printf("[bookshop] cannot allocate %u bytes for catalog\n", (unsigned)file_size);
        return -1;
    }

    FILE *fp = fopen(CATALOG_PATH, "r");
    if (!fp) {
        printf("[bookshop] fopen(%s) failed\n", CATALOG_PATH);
        free(catalog_buf);
        return -1;
    }
    size_t len = fread(catalog_buf, 1, file_size, fp);
    fclose(fp);

    if (len == 0) {
        printf("[bookshop] empty catalog file\n");
        free(catalog_buf);
        return -1;
    }
    catalog_buf[len] = '\0';
    printf("[bookshop] fread %u bytes from catalog file\n", (unsigned)len);

    book_count = 0;
    for (int i = 0; i < MAX_BOOKS; i++) {
        char query[32];
        snprintf(query, sizeof(query), "[%d].title", i);
        char *val;
        size_t vallen;
        JSONTypes_t type;
        JSONStatus_t s = JSON_SearchT(catalog_buf, len, query, strlen(query), &val, &vallen, &type);
        if (s != JSONSuccess) {
            printf("[bookshop] JSON_Search [%d].title returned %d, stopping\n", i, s);
            break;
        }

        book_entry_t *book = &books[book_count];
        extract_string(val, vallen, book->title, sizeof(book->title));

        snprintf(query, sizeof(query), "[%d].author", i);
        s = JSON_SearchT(catalog_buf, len, query, strlen(query), &val, &vallen, &type);
        if (s == JSONSuccess) extract_string(val, vallen, book->author, sizeof(book->author));
        else {
            printf("[bookshop] author not found for book %d\n", i);
            book->author[0] = '\0';
        }

        snprintf(query, sizeof(query), "[%d].markdown_file", i);
        s = JSON_SearchT(catalog_buf, len, query, strlen(query), &val, &vallen, &type);
        if (s == JSONSuccess) extract_string(val, vallen, book->markdown_file, sizeof(book->markdown_file));
        else {
            printf("[bookshop] markdown_file not found for book %d\n", i);
            book->markdown_file[0] = '\0';
        }

        snprintf(query, sizeof(query), "[%d].size", i);
        s = JSON_SearchT(catalog_buf, len, query, strlen(query), &val, &vallen, &type);
        book->size = (s == JSONSuccess) ? atoi(val) : 0;

        printf("[bookshop] book %d: title='%s' author='%s' file='%s' size=%d\n",
               i, book->title, book->author, book->markdown_file, book->size);
        book_count++;
    }

    printf("[bookshop] parsed %d books\n", book_count);
    free(catalog_buf);
    if (book_count > 0) {
        qsort(books, book_count, sizeof(book_entry_t), compare_books);
        printf("[bookshop] sorted %d books alphabetically\n", book_count);
    }
    return book_count;
}

static void build_local_book_filename(const char *src_name, char *dst_name, size_t dst_size) {
    if (!dst_name || dst_size == 0) return;
    if (!src_name || !src_name[0]) {
        snprintf(dst_name, dst_size, "book.md");
        return;
    }

    size_t out_index = 0;
    for (size_t src_index = 0; src_name[src_index] && out_index < dst_size - 1; src_index++) {
        unsigned char raw_char = (unsigned char)src_name[src_index];

        // Keep a conservative ASCII subset for FATFS path safety.
        if ((raw_char >= 'A' && raw_char <= 'Z') ||
            (raw_char >= 'a' && raw_char <= 'z') ||
            (raw_char >= '0' && raw_char <= '9') ||
            raw_char == '-' || raw_char == '_' || raw_char == '.' || raw_char == ' ' || raw_char == '\'') {
            dst_name[out_index++] = (char)raw_char;
        } else {
            dst_name[out_index++] = '_';
        }
    }
    dst_name[out_index] = '\0';

    // Avoid empty or dot-only names.
    if (dst_name[0] == '\0' || strcmp(dst_name, ".") == 0 || strcmp(dst_name, "..") == 0) {
        snprintf(dst_name, dst_size, "book.md");
        return;
    }

    // Ensure extension remains readable for reader app UX.
    if (!strstr(dst_name, ".md")) {
        size_t current_len = strlen(dst_name);
        if (current_len + 3 < dst_size) {
            strcat(dst_name, ".md");
        }
    }
}

static void build_local_book_path(const book_entry_t *book, char *path_out, size_t path_out_size) {
    if (!book || !path_out || path_out_size == 0) return;
    char safe_name[160];
    build_local_book_filename(book->markdown_file, safe_name, sizeof(safe_name));
    snprintf(path_out, path_out_size, "%s/%s", SD_BOOKS_DIR, safe_name);
}

static int load_catalog(void) {
    text_mode_clear(TEXT_COLOR_BLACK);
    text_mode_print_at_attr(0, 0, "Checking catalog cache...", TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);
    text_mode_flush();

    if (config_get_int("catalog_dl_pending", 0)) {
        config_delete("catalog_dl_pending");
        if (!file_exists(CATALOG_PATH)) {
            printf("[bookshop] Catalog download failed\n");
            return -1;
        }
        printf("[bookshop] Catalog downloaded, loading from disk\n");
        int result = load_catalog_from_disk();
        if (result > 0) {
            return result;
        }
    }

    if (is_catalog_fresh()) {
        text_mode_print_at_attr(0, 1, "Loading cached catalog...", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
        text_mode_flush();
        printf("[bookshop] Catalog is fresh, loading from disk\n");
        int result = load_catalog_from_disk();
        if (result > 0) {
            return result;
        }
        printf("[bookshop] Failed to load fresh catalog, will try download\n");
    }

    printf("[bookshop] Catalog missing or stale, requesting OS download\n");
    text_mode_print_at_attr(0, 1, "Requesting OS download...", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
    text_mode_flush();
    config_set_int("catalog_dl_pending", 1);
    if (!os_download_via_os(CATALOG_URL, CATALOG_PATH, 0)) {
        config_delete("catalog_dl_pending");
        printf("[bookshop] Failed to queue OS download\n");
        return -1;
    }

    printf("[bookshop] OS download requested, app will relaunch after download\n");
    return -2;
}

static void draw_progress_bar(int bar_x, int bar_y, int bar_width, int percent) {
    for (int x = 0; x < bar_width; x++) {
        text_mode_print_at_color(bar_x + x, bar_y,
            x < (percent * bar_width / 100) ? "=" : "-",
            x < (percent * bar_width / 100) ? TEXT_COLOR_GREEN : TEXT_COLOR_CYAN);
    }
    char pct_str[16];
    snprintf(pct_str, sizeof(pct_str), "%d%%", percent);
    text_mode_print_at_attr(bar_x, bar_y - 1, pct_str, TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    text_mode_flush();
}

static void download_progress(int percent, const char *status) {
    if (percent < 0) return;
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int bar_width = cols - 8;
    if (bar_width > 50) bar_width = 50;
    int bar_x = (cols - bar_width) / 2;
    int bar_y = rows / 2 + 2;
    draw_progress_bar(bar_x, bar_y, bar_width, percent);
}

static void rebuild_catalog_ui(void) {
    for (int i = 0; i < book_count; i++) {
        int n = snprintf(list_texts[i], DISPLAY_LEN, "%s - %s", books[i].title, books[i].author);
        if (n >= DISPLAY_LEN) {
            list_texts[i][DISPLAY_LEN - 1] = '\0';
        }
        list_items[i] = list_texts[i];
        char sd_path[320];
        build_local_book_path(&books[i], sd_path, sizeof(sd_path));
        row_attrs[i] = file_exists(sd_path) ? TEXT_ATTR_NORMAL : TEXT_ATTR_BOLD;
    }

    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();
    int btn_h = 3;

    ui2_layout_t *root = ui2_layout_create(0, 0, cols, rows, UI2_LAYOUT_VERTICAL);
    ui2_layout_set_gap(root, 1);

    ui2_label_t *title = ui2_label_create(0, 0, "Book Shop",
                                           TEXT_COLOR_BRIGHT_CYAN, TEXT_ATTR_BOLD);
    ui2_layout_add(root, UI2_WIDGET(title));

    book_list = ui2_list_create(0, 0, cols, rows - btn_h - 3);
    ui2_list_set_title(book_list, "Available Books");
    ui2_list_set_colors(book_list, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK,
                        TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_BLUE, TEXT_COLOR_CYAN);
    ui2_list_set_border(book_list, true);
    ui2_list_set_items(book_list, (const char **)list_items, book_count);
    ui2_list_set_row_attrs(book_list, row_attrs, book_count);
    ui2_list_set_callbacks(book_list, NULL, on_book_activated, NULL);
    ui2_layout_add(root, UI2_WIDGET(book_list));

    ui2_layout_t *btn_row = ui2_layout_create(0, 0, cols, btn_h, UI2_LAYOUT_HORIZONTAL);
    ui2_layout_set_gap(btn_row, 1);

    int btn_w = 3;
    ui2_button_t *btn_up = ui2_button_create(0, 0, btn_w, btn_h, ICON_ARROW_BIG_UP);
    ui2_button_set_callback(btn_up, on_up_click, NULL);

    ui2_button_t *btn_down = ui2_button_create(0, 0, btn_w, btn_h, ICON_ARROW_BIG_DOWN);
    ui2_button_set_callback(btn_down, on_down_click, NULL);

    ui2_button_t *btn_get = ui2_button_create(0, 0, btn_w, btn_h, ICON_ARROW_DOWN_TO_LINE);
    ui2_button_set_callback(btn_get, on_download_click, NULL);

    ui2_button_t *btn_exit = ui2_button_create(0, 0, btn_w, btn_h, ICON_X);
    ui2_button_set_callback(btn_exit, on_exit_click, NULL);

    ui2_layout_add(btn_row, UI2_WIDGET(btn_up));
    ui2_layout_add(btn_row, UI2_WIDGET(btn_down));
    ui2_layout_add(btn_row, UI2_WIDGET(btn_get));
    ui2_layout_add(btn_row, UI2_WIDGET(btn_exit));

    ui2_layout_add(root, UI2_WIDGET(btn_row));

    ui2_screen_set_root(screen, root);
    ui2_screen_focus_set(screen, UI2_WIDGET(book_list));
}

static void show_pending_download_result(void) {
    int result = appcfg_get_int(DOWNLOAD_RESULT_KEY, 0);
    if (result == 0) {
        return;
    }

    config_delete(DOWNLOAD_RESULT_KEY);

    if (result > 0) {
        char downloaded_path[320];
        appcfg_get_string(DOWNLOAD_PATH_KEY, "", downloaded_path, sizeof(downloaded_path));
        config_delete(DOWNLOAD_PATH_KEY);
        ui2_screen_toast_show(screen, "\xE2\x9C\x93 Downloaded!", TEXT_COLOR_BLACK, TEXT_COLOR_GREEN, 6);
    } else {
        char err[32];
        snprintf(err, sizeof(err), "Failed (%d)", result);
        ui2_screen_toast_show(screen, err, TEXT_COLOR_BLACK, TEXT_COLOR_RED, 6);
    }
}

static void on_up_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    if (!book_list) return;
    int sel = ui2_list_get_selection(book_list);
    if (sel > 0) {
        ui2_list_set_selection(book_list, sel - 1);
        ui2_screen_render(screen);
    }
}

static void on_down_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    if (!book_list) return;
    int sel = ui2_list_get_selection(book_list);
    if (sel < book_count - 1) {
        ui2_list_set_selection(book_list, sel + 1);
        ui2_screen_render(screen);
    }
}

static void on_download_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    if (!book_list) return;
    int sel = ui2_list_get_selection(book_list);
    if (sel >= 0 && sel < book_count) {
        on_book_activated(sel, NULL);
    }
}

static void on_book_activated(int index, void *data) {
    (void)data;
    if (index < 0 || index >= book_count) return;

    book_entry_t *book = &books[index];
    int cols = text_mode_get_cols();
    int rows = text_mode_get_rows();

    text_mode_clear(TEXT_COLOR_BLACK);

    int cx = cols / 2;
    int win_y = rows / 2 - 4;
    text_mode_print_at_attr(cx - (int)strlen(book->title) / 2, win_y, book->title,
                            TEXT_COLOR_BRIGHT_WHITE, TEXT_ATTR_BOLD);
    text_mode_print_at_attr(cx - (int)strlen(book->author) / 2, win_y + 1, book->author,
                            TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);

    build_local_book_path(book, download_sd_path, sizeof(download_sd_path));

    if (file_exists(download_sd_path)) {
        ui2_screen_toast_show(screen, "Already downloaded!", TEXT_COLOR_BLACK, TEXT_COLOR_GREEN, 8);
        ui2_screen_render(screen);
        return;
    }

    char size_str[32];
    if (book->size > 0) {
        snprintf(size_str, sizeof(size_str), "Size: %d KB", book->size / 1024);
        text_mode_print_at_attr(cx - (int)strlen(size_str) / 2, win_y + 3, size_str,
                                TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
    }

    text_mode_print_at_attr(cx - 7, win_y + 4, "Downloading...",
                            TEXT_COLOR_YELLOW, TEXT_ATTR_NORMAL);

    int bar_width = cols - 8;
    if (bar_width > 50) bar_width = 50;
    int bar_x = (cols - bar_width) / 2;
    int bar_y = win_y + 6;
    for (int x = 0; x < bar_width; x++)
        text_mode_print_at_color(bar_x + x, bar_y, "-", TEXT_COLOR_CYAN);
    text_mode_flush();

    url_encode(book->markdown_file, download_encoded, sizeof(download_encoded));
    snprintf(download_url, sizeof(download_url), "%s%s", BOOKS_BASE, download_encoded);

    mkdir(SD_BOOKS_DIR, 0755);

    mode = MODE_DOWNLOADING;
    if (!os_download_via_os(download_url, download_sd_path, (size_t)book->size)) {
        mode = MODE_CATALOG;
        ui2_screen_toast_show(screen, "Queue failed", TEXT_COLOR_BLACK, TEXT_COLOR_RED, 6);
        ui2_screen_render(screen);
    }
}

static void on_exit_click(ui2_button_t *btn, void *data) {
    (void)btn;
    (void)data;
    os_exit();
}

void app_init(app_context_t *ctx) {
    ctx->subscriptions = EVENT_KEYBOARD | EVENT_TOUCH | EVENT_TIMER;
    ctx->timer_interval_ms = 250;
    text_mode_init();

    screen = ui2_screen_create();
    mode = MODE_CATALOG;
    book_count = 0;
    ignore_events = 0;

    show_pending_download_result();

    int n = load_catalog();
    if (n > 0) {
        rebuild_catalog_ui();
    } else if (n == -2) {
        mode = MODE_ERROR;
        snprintf(error_msg, sizeof(error_msg), "Downloading catalog...");
        text_mode_clear(TEXT_COLOR_BLACK);
        text_mode_print_at_attr(0, 2, error_msg, TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
        text_mode_flush();
        return;
    } else {
        mode = MODE_ERROR;
        snprintf(error_msg, sizeof(error_msg), "Failed to load catalog (%d)", n);
        text_mode_clear(TEXT_COLOR_BLACK);
        text_mode_print_at_attr(0, 1, error_msg, TEXT_COLOR_RED, TEXT_ATTR_NORMAL);
        text_mode_print_at_attr(0, 2, "Check WiFi and try again", TEXT_COLOR_WHITE, TEXT_ATTR_NORMAL);
        text_mode_print_at_attr(0, 3, "Press any key to exit", TEXT_COLOR_CYAN, TEXT_ATTR_NORMAL);
        text_mode_flush();
        return;
    }

    ui2_screen_render(screen);
}

void app_event(app_context_t *ctx, event_t *event) {
    (void)ctx;
    if (ignore_events > 0) {
        ignore_events--;
        return;
    }
    if (event->type == EVENT_TIMER) {
        if (ui2_screen_toast_active(screen)) {
            ui2_screen_toast_tick(screen);
            if (!ui2_screen_toast_active(screen)) {
                ui2_screen_render(screen);
            }
        }
        return;
    }
    if (mode == MODE_ERROR) {
        if (event->type == EVENT_KEYBOARD || event->type == EVENT_TOUCH) {
            os_exit();
        }
        return;
    }
    if (mode == MODE_DOWNLOADING) return;
    if (ui2_screen_handle_event(screen, event))
        ui2_screen_render(screen);
}

void app_checkpoint(app_context_t *ctx) {
    (void)ctx;
}

void app_close(app_context_t *ctx) {
    (void)ctx;
    if (screen) {
        ui2_screen_destroy(screen);
        screen = NULL;
    }
    text_mode_clear(TEXT_COLOR_BLACK);
}

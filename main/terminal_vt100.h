#ifndef VT100_H
#define VT100_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// VT100 screen size limits
#define TERM_MAX_COLS 80
#define TERM_MAX_ROWS 40  // Support tomthumb font (80x40 grid)
#define TERM_MAX_BUFFER_SIZE (TERM_MAX_COLS * TERM_MAX_ROWS)

// VT100 colors
typedef enum {
    VT100_COLOR_BLACK = 0,
    VT100_COLOR_RED = 1,
    VT100_COLOR_GREEN = 2,
    VT100_COLOR_YELLOW = 3,
    VT100_COLOR_BLUE = 4,
    VT100_COLOR_MAGENTA = 5,
    VT100_COLOR_CYAN = 6,
    VT100_COLOR_WHITE = 7
} vt100_color_t;

// Character attributes using bit fields
typedef struct {
    unsigned int bold      : 1;
    unsigned int underline : 1;
    unsigned int italic    : 1;
    unsigned int reverse   : 1;
    unsigned int blink     : 1;
    unsigned int graphics  : 1;
    unsigned int fg        : 3;
    unsigned int bg        : 3;
} vt100_attr_t;

#define vt100_attr_init() {0, 0, 0, 0, 0, 0, VT100_COLOR_WHITE, VT100_COLOR_BLACK}

// Callback typedefs
typedef void (*vt100_write_cb)(const char *data, size_t len);
typedef void (*vt100_title_cb)(const char *title);

// Parser state
typedef enum {
    VT100_STATE_GROUND,
    VT100_STATE_ESCAPE,
    VT100_STATE_CSI,
    VT100_STATE_OSC,
    VT100_STATE_CHARSET
} vt100_state_t;

// VT100 terminal state
typedef struct {
    // Screen buffer
    char screen[TERM_MAX_BUFFER_SIZE];
    vt100_attr_t attrs[TERM_MAX_BUFFER_SIZE];

    // Active geometry
    int cols;
    int rows;
    int buffer_size;

    // Cursor position
    int cursor_x;
    int cursor_y;

    // Saved cursor position
    int saved_cursor_x;
    int saved_cursor_y;
    vt100_attr_t saved_attr;
    int saved_graphics_mode;
    int saved_origin_mode;

    // Scroll region
    int scroll_top;
    int scroll_bottom;

    // Modes
    int origin_mode;
    int line_feed_mode;
    int auto_wrap;
    int screen_reverse;
    int app_cursor_keys;
    int cursor_visible;
    int insert_mode;

    // Tab stops
    char tab_stops[TERM_MAX_COLS];

    // Parser state
    vt100_state_t state;
    char escape_buf[32];
    int escape_pos;
    char flag;

    // Current attributes
    vt100_attr_t current_attr;

    // Charset state
    int graphics_mode;

    // UTF-8 state
    int utf8_remaining;
    uint32_t utf8_codepoint;

    // Flags
    int needs_redraw;

    // Callbacks
    vt100_write_cb write_callback;
    vt100_title_cb title_callback;
} vt100_t;

// Initialize VT100 terminal
void vt100_init(vt100_t *vt, vt100_write_cb write_cb);

// Set callbacks
void vt100_set_write_callback(vt100_t *vt, vt100_write_cb cb);
void vt100_set_title_callback(vt100_t *vt, vt100_title_cb cb);

// Process incoming character
void vt100_process(vt100_t *vt, char c);

// Get screen contents
char vt100_get_char(const vt100_t *vt, int x, int y);
vt100_attr_t vt100_get_attr(const vt100_t *vt, int x, int y);

// Get screen contents with origin mode applied
char vt100_get_char_with_origin(const vt100_t *vt, int x, int y);
vt100_attr_t vt100_get_attr_with_origin(const vt100_t *vt, int x, int y);

// Cursor position
int vt100_cursor_x(const vt100_t *vt);
int vt100_cursor_y(const vt100_t *vt);

// Redraw flag
int vt100_needs_redraw(const vt100_t *vt);
void vt100_clear_redraw_flag(vt100_t *vt);

// Clear screen
void vt100_clear_screen(vt100_t *vt);

// Set terminal geometry
void vt100_set_geometry(vt100_t *vt, int cols, int rows);

// Get terminal size
int vt100_cols(const vt100_t *vt);
int vt100_rows(const vt100_t *vt);

// Mode queries
int vt100_line_feed_mode(const vt100_t *vt);
int vt100_screen_reverse(const vt100_t *vt);
int vt100_app_cursor_keys(const vt100_t *vt);
int vt100_cursor_visible(const vt100_t *vt);

#ifdef __cplusplus
}
#endif

#endif // VT100_H

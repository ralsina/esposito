#include "terminal_vt100.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Forward declarations for static functions
static void handleChar(vt100_t *vt, char c);
static void handleCodepoint(vt100_t *vt, uint32_t cp);
static void handleEscape(vt100_t *vt, char c);
static void executeCSI(vt100_t *vt, const char *seq, int len);
static void setCursor(vt100_t *vt, int x, int y);
static void setChar(vt100_t *vt, int x, int y, char c);
static void advanceCursor(vt100_t *vt);
static void newline(vt100_t *vt);
static void scrollUp(vt100_t *vt);
static void scrollDown(vt100_t *vt);
static void applyOriginMode(const vt100_t *vt, int *x, int *y);
static int xyToIndex(const vt100_t *vt, int x, int y);

// Simple integer min/max/constrain for embedded use
#define VT100_MIN(a,b) ((a) < (b) ? (a) : (b))
#define VT100_MAX(a,b) ((a) > (b) ? (a) : (b))
#define VT100_CONSTRAIN(amt,low,high) \
    ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

// Maps Unicode box-drawing codepoints (U+2500..U+257F) to VT100 ACS letters.
static const char boxDrawingACS[128] = {
 // 2500  2501  2502  2503  2504  2505  2506  2507
    'q',  'q',  'x',  'x',  'q',  'q',  'x',  'x',
 // 2508  2509  250A  250B  250C  250D  250E  250F
    'q',  'q',  'x',  'x',  'l',  'l',  'l',  'l',
 // 2510  2511  2512  2513  2514  2515  2516  2517
    'k',  'k',  'k',  'k',  'm',  'm',  'm',  'm',
 // 2518  2519  251A  251B  251C  251D  251E  251F
    'j',  'j',  'j',  'j',  't',  't',  't',  't',
 // 2520  2521  2522  2523  2524  2525  2526  2527
    't',  't',  't',  't',  'u',  'u',  'u',  'u',
 // 2528  2529  252A  252B  252C  252D  252E  252F
    'u',  'u',  'u',  'u',  'w',  'w',  'w',  'w',
 // 2530  2531  2532  2533  2534  2535  2536  2537
    'w',  'w',  'w',  'w',  'v',  'v',  'v',  'v',
 // 2538  2539  253A  253B  253C  253D  253E  253F
    'v',  'v',  'v',  'v',  'n',  'n',  'n',  'n',
 // 2540  2541  2542  2543  2544  2545  2546  2547
    'n',  'n',  'n',  'n',  'n',  'n',  'n',  'n',
 // 2548  2549  254A  254B  254C  254D  254E  254F
    'n',  'n',  'n',  'n',  'q',  'q',  'x',  'x',
 // 2550  2551  2552  2553  2554  2555  2556  2557
    'q',  'x',  'l',  'l',  'l',  'k',  'k',  'k',
 // 2558  2559  255A  255B  255C  255D  255E  255F
    'm',  'm',  'm',  'j',  'j',  'j',  't',  't',
 // 2560  2561  2562  2563  2564  2565  2566  2567
    't',  't',  'u',  'u',  'u',  'w',  'w',  'w',
 // 2568  2569  256A  256B  256C  256D  256E  256F
    'v',  'v',  'v',  'n',  'n',  'n',  'l',  'k',
 // 2570  2571  2572  2573  2574  2575  2576  2577
    'j',  'm',   0,    0,    0,    0,    0,    0,
 // 2578  2579  257A  257B  257C  257D  257E  257F
     0,    0,    0,    0,    0,    0,    0,    0,
};

void vt100_init(vt100_t *vt, vt100_write_cb write_cb) {
    vt->cursor_x = 0;
    vt->cursor_y = 0;
    vt->cols = 64;
    vt->rows = 30;
    vt->buffer_size = 64 * 30;
    vt->saved_cursor_x = 0;
    vt->saved_cursor_y = 0;
    vt->saved_graphics_mode = 0;
    vt->saved_origin_mode = 0;
    vt->scroll_top = 0;
    vt->scroll_bottom = vt->rows - 1;
    vt->origin_mode = 0;
    vt->line_feed_mode = 0;
    vt->auto_wrap = 1;
    vt->screen_reverse = 0;
    vt->app_cursor_keys = 0;
    vt->cursor_visible = 1;
    vt->insert_mode = 0;
    vt->state = VT100_STATE_GROUND;
    vt->escape_pos = 0;
    vt->needs_redraw = 0;
    vt->flag = '\0';
    vt->graphics_mode = 0;
    vt->utf8_remaining = 0;
    vt->utf8_codepoint = 0;
    vt->write_callback = write_cb;
    vt->title_callback = NULL;

    memset(vt->screen, ' ', TERM_MAX_BUFFER_SIZE);
    for (int i = 0; i < vt->buffer_size; i++) {
        vt->attrs[i] = (vt100_attr_t)vt100_attr_init();
    }

    vt->current_attr = (vt100_attr_t)vt100_attr_init();

    // Initialize tab stops to every 8 columns
    for (int i = 0; i < TERM_MAX_COLS; i++) {
        vt->tab_stops[i] = (i % 8 == 0 && i > 0) ? 1 : 0;
    }
}

void vt100_set_write_callback(vt100_t *vt, vt100_write_cb cb) {
    vt->write_callback = cb;
}

void vt100_set_title_callback(vt100_t *vt, vt100_title_cb cb) {
    vt->title_callback = cb;
}

static int xyToIndex(const vt100_t *vt, int x, int y) {
    return y * vt->cols + x;
}

static void setChar(vt100_t *vt, int x, int y, char c) {
    int idx = xyToIndex(vt, x, y);
    if (idx >= 0 && idx < vt->buffer_size) {
        vt->screen[idx] = c;
        vt->attrs[idx] = vt->current_attr;
        vt->attrs[idx].graphics = vt->graphics_mode;
    }
}

static void applyOriginMode(const vt100_t *vt, int *x, int *y) {
    if (vt->origin_mode) {
        *x = VT100_CONSTRAIN(*x, 0, vt->cols - 1);
        *y = VT100_CONSTRAIN(*y + vt->scroll_top, vt->scroll_top, vt->scroll_bottom);
    } else {
        *x = VT100_CONSTRAIN(*x, 0, vt->cols - 1);
        *y = VT100_CONSTRAIN(*y, 0, vt->rows - 1);
    }
}

static void scrollUp(vt100_t *vt) {
    if (vt->scroll_top < vt->scroll_bottom) {
        int lines = vt->scroll_bottom - vt->scroll_top;
        memmove(&vt->screen[vt->scroll_top * vt->cols],
                &vt->screen[(vt->scroll_top + 1) * vt->cols],
                lines * vt->cols);
        memmove(&vt->attrs[vt->scroll_top * vt->cols],
                &vt->attrs[(vt->scroll_top + 1) * vt->cols],
                lines * vt->cols * sizeof(vt100_attr_t));

        for (int x = 0; x < vt->cols; x++) {
            setChar(vt, x, vt->scroll_bottom, ' ');
        }
    }
}

static void scrollDown(vt100_t *vt) {
    if (vt->scroll_top < vt->scroll_bottom) {
        int lines = vt->scroll_bottom - vt->scroll_top;
        memmove(&vt->screen[(vt->scroll_top + 1) * vt->cols],
                &vt->screen[vt->scroll_top * vt->cols],
                lines * vt->cols);
        memmove(&vt->attrs[(vt->scroll_top + 1) * vt->cols],
                &vt->attrs[vt->scroll_top * vt->cols],
                lines * vt->cols * sizeof(vt100_attr_t));

        for (int x = 0; x < vt->cols; x++) {
            setChar(vt, x, vt->scroll_top, ' ');
        }
    }
}

static void newline(vt100_t *vt) {
    if (vt->cursor_y == vt->scroll_bottom) {
        scrollUp(vt);
    } else {
        vt->cursor_y = VT100_MIN(vt->cursor_y + 1, vt->rows - 1);
    }
}

static void advanceCursor(vt100_t *vt) {
    vt->cursor_x++;
    if (vt->cursor_x >= vt->cols) {
        if (vt->auto_wrap) {
            vt->cursor_x = 0;
            newline(vt);
        } else {
            vt->cursor_x = vt->cols - 1;
        }
    }
}

static void setCursor(vt100_t *vt, int x, int y) {
    applyOriginMode(vt, &x, &y);
    vt->cursor_x = VT100_CONSTRAIN(x, 0, vt->cols - 1);
    vt->cursor_y = VT100_CONSTRAIN(y, 0, vt->rows - 1);
}

static void handleCodepoint(vt100_t *vt, uint32_t cp) {
    if (cp < 0x80) {
        handleChar(vt, (char)cp);
        return;
    }
    if (cp >= 0x2500 && cp <= 0x257F) {
        char acsChar = boxDrawingACS[cp - 0x2500];
        if (acsChar) {
            int savedGraphics = vt->graphics_mode;
            vt->graphics_mode = 1;
            handleChar(vt, acsChar);
            vt->graphics_mode = savedGraphics;
            return;
        }
    }
    // Unknown codepoint: advance cursor with space
    handleChar(vt, ' ');
}

void vt100_set_geometry(vt100_t *vt, int cols, int rows) {
    vt->cols = VT100_CONSTRAIN(cols, 1, TERM_MAX_COLS);
    vt->rows = VT100_CONSTRAIN(rows, 1, TERM_MAX_ROWS);
    vt->buffer_size = vt->cols * vt->rows;
    vt100_clear_screen(vt);
    if (vt->write_callback) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "\033[8;%d;%dt", vt->rows, vt->cols);
        vt->write_callback(buf, (size_t)len);
    }
}

static void handleChar(vt100_t *vt, char c) {
    switch (c) {
        case '\r':
            vt->cursor_x = 0;
            if (vt->line_feed_mode) {
                newline(vt);
            }
            break;

        case '\n':
            newline(vt);
            break;

        case '\016':  // SO - Shift Out: switch to G1 (graphics) charset
            vt->graphics_mode = 1;
            break;

        case '\017':  // SI - Shift In: switch to G0 (ASCII) charset
            vt->graphics_mode = 0;
            break;

        case '\t': {
            int next = vt->cursor_x + 1;
            while (next < vt->cols && !vt->tab_stops[next]) {
                next++;
            }
            vt->cursor_x = (next < vt->cols) ? next : vt->cols - 1;
            break;
        }

        case '\b':
            if (vt->cursor_x > 0) {
                vt->cursor_x--;
            }
            break;

        case '\007':  // Bell
            break;

        default:
            if (c >= 32 && c <= 126) {
                if (vt->insert_mode) {
                    for (int x = vt->cols - 1; x > vt->cursor_x; x--) {
                        int src = xyToIndex(vt, x - 1, vt->cursor_y);
                        int dst = xyToIndex(vt, x, vt->cursor_y);
                        vt->screen[dst] = vt->screen[src];
                        vt->attrs[dst] = vt->attrs[src];
                    }
                }
                setChar(vt, vt->cursor_x, vt->cursor_y, c);
                advanceCursor(vt);
            }
            break;
    }
}

static void executeCSI(vt100_t *vt, const char *seq, int len) {
    int params[10] = {0};
    int paramCount = 0;
    vt->flag = '\0';

    const char *p = seq + 2;
    const char *end = seq + len;
    char command = end[-1];

    if (*p == '?') {
        vt->flag = '?';
        p++;
    } else if (*p == '>') {
        vt->flag = '>';
        p++;
    }

    while (p < end - 1 && paramCount < 10) {
        if (*p >= '0' && *p <= '9') {
            params[paramCount] = atoi(p);
            paramCount++;
            while (p < end && *p >= '0' && *p <= '9') p++;
        } else if (*p == ';') {
            p++;
        } else {
            p++;
        }
    }

    switch (command) {
        case 'A': {
            int n = (params[0] > 0) ? params[0] : 1;
            int topLimit = vt->origin_mode ? vt->scroll_top : 0;
            vt->cursor_y = VT100_MAX(topLimit, vt->cursor_y - n);
            break;
        }

        case 'B': {
            int n = (params[0] > 0) ? params[0] : 1;
            int bottomLimit = vt->origin_mode ? vt->scroll_bottom : vt->rows - 1;
            vt->cursor_y = VT100_MIN(bottomLimit, vt->cursor_y + n);
            break;
        }

        case 'C': {
            int n = (params[0] > 0) ? params[0] : 1;
            vt->cursor_x = VT100_MIN(vt->cols - 1, vt->cursor_x + n);
            break;
        }

        case 'D': {
            int n = (params[0] > 0) ? params[0] : 1;
            vt->cursor_x = VT100_MAX(0, vt->cursor_x - n);
            break;
        }

        case 'E': {
            int n = (params[0] > 0) ? params[0] : 1;
            int bottomLimit = vt->origin_mode ? vt->scroll_bottom : vt->rows - 1;
            vt->cursor_y = VT100_MIN(bottomLimit, vt->cursor_y + n);
            vt->cursor_x = 0;
            break;
        }

        case 'F': {
            int n = (params[0] > 0) ? params[0] : 1;
            int topLimit = vt->origin_mode ? vt->scroll_top : 0;
            vt->cursor_y = VT100_MAX(topLimit, vt->cursor_y - n);
            vt->cursor_x = 0;
            break;
        }

        case 'G': {
            int col = (params[0] > 0) ? params[0] : 1;
            vt->cursor_x = VT100_CONSTRAIN(col - 1, 0, vt->cols - 1);
            break;
        }

        case 'd': {
            int row = (params[0] > 0) ? params[0] : 1;
            vt->cursor_y = VT100_CONSTRAIN(row - 1, 0, vt->rows - 1);
            break;
        }

        case 'I': {
            int n = (params[0] > 0) ? params[0] : 1;
            for (int t = 0; t < n; t++) {
                int next = vt->cursor_x + 1;
                while (next < vt->cols && !vt->tab_stops[next]) next++;
                vt->cursor_x = (next < vt->cols) ? next : vt->cols - 1;
            }
            break;
        }

        case 'Z': {
            int n = (params[0] > 0) ? params[0] : 1;
            for (int t = 0; t < n; t++) {
                int prev = vt->cursor_x - 1;
                while (prev > 0 && !vt->tab_stops[prev]) prev--;
                vt->cursor_x = (prev >= 0) ? prev : 0;
            }
            break;
        }

        case 'H':
        case 'f': {
            int row = (params[0] > 0) ? params[0] : 1;
            int col = (paramCount > 1 && params[1] > 0) ? params[1] : 1;
            setCursor(vt, col - 1, row - 1);
            break;
        }

        case 'r': {
            int top = (params[0] > 0) ? params[0] : 1;
            int bottom = (paramCount > 1 && params[1] > 0) ? params[1] : vt->rows;
            vt->scroll_top = VT100_CONSTRAIN(top - 1, 0, vt->rows - 1);
            vt->scroll_bottom = VT100_CONSTRAIN(bottom - 1, 0, vt->rows - 1);
            if (vt->scroll_top >= vt->scroll_bottom) {
                vt->scroll_top = 0;
                vt->scroll_bottom = vt->rows - 1;
            }
            setCursor(vt, 0, 0);
            break;
        }

        case 'J': {
            int mode = (params[0] > 0) ? params[0] : 0;
            if (mode == 0) {
                for (int y = vt->cursor_y; y < vt->rows; y++) {
                    int startX = (y == vt->cursor_y) ? vt->cursor_x : 0;
                    for (int x = startX; x < vt->cols; x++) {
                        setChar(vt, x, y, ' ');
                    }
                }
            } else if (mode == 1) {
                for (int y = 0; y <= vt->cursor_y; y++) {
                    int endX = (y == vt->cursor_y) ? vt->cursor_x + 1 : vt->cols;
                    for (int x = 0; x < endX; x++) {
                        setChar(vt, x, y, ' ');
                    }
                }
            } else if (mode == 2 || mode == 3) {
                vt100_clear_screen(vt);
            }
            break;
        }

        case 'K': {
            int mode = (params[0] > 0) ? params[0] : 0;
            if (mode == 0) {
                for (int x = vt->cursor_x; x < vt->cols; x++) {
                    setChar(vt, x, vt->cursor_y, ' ');
                }
            } else if (mode == 1) {
                for (int x = 0; x <= vt->cursor_x; x++) {
                    setChar(vt, x, vt->cursor_y, ' ');
                }
            } else if (mode == 2) {
                for (int x = 0; x < vt->cols; x++) {
                    setChar(vt, x, vt->cursor_y, ' ');
                }
            }
            break;
        }

        case 'm': {
            if (paramCount == 0) {
                vt->current_attr = (vt100_attr_t)vt100_attr_init();
            } else {
                for (int i = 0; i < paramCount; i++) {
                    int code = params[i];
                    if (code == 0) {
                        vt->current_attr = (vt100_attr_t)vt100_attr_init();
                    } else if (code == 1) {
                        vt->current_attr.bold = 1;
                    } else if (code == 2) {
                        vt->current_attr.bold = 0;
                    } else if (code == 3) {
                        vt->current_attr.italic = 1;
                    } else if (code == 4) {
                        vt->current_attr.underline = 1;
                    } else if (code == 5 || code == 6) {
                        vt->current_attr.blink = 1;
                    } else if (code == 7) {
                        vt->current_attr.reverse = 1;
                    } else if (code == 22) {
                        vt->current_attr.bold = 0;
                    } else if (code == 23) {
                        vt->current_attr.italic = 0;
                    } else if (code == 24) {
                        vt->current_attr.underline = 0;
                    } else if (code == 25) {
                        vt->current_attr.blink = 0;
                    } else if (code == 27) {
                        vt->current_attr.reverse = 0;
                    } else if (code >= 30 && code <= 37) {
                        vt->current_attr.fg = (unsigned int)(code - 30);
                    } else if (code >= 40 && code <= 47) {
                        vt->current_attr.bg = (unsigned int)(code - 40);
                    }
                }
            }
            break;
        }

        case 'L': {
            int n = (params[0] > 0) ? params[0] : 1;
            if (vt->cursor_y >= vt->scroll_top && vt->cursor_y <= vt->scroll_bottom) {
                int moveLines = vt->scroll_bottom - vt->cursor_y - n + 1;
                if (moveLines > 0) {
                    memmove(&vt->screen[(vt->cursor_y + n) * vt->cols],
                            &vt->screen[vt->cursor_y * vt->cols],
                            moveLines * vt->cols);
                    memmove(&vt->attrs[(vt->cursor_y + n) * vt->cols],
                            &vt->attrs[vt->cursor_y * vt->cols],
                            moveLines * vt->cols * sizeof(vt100_attr_t));
                }
                int clearLines = VT100_MIN(n, vt->scroll_bottom - vt->cursor_y + 1);
                for (int row = vt->cursor_y; row < vt->cursor_y + clearLines; row++) {
                    for (int col = 0; col < vt->cols; col++) {
                        setChar(vt, col, row, ' ');
                    }
                }
            }
            break;
        }

        case 'M': {
            int n = (params[0] > 0) ? params[0] : 1;
            if (vt->cursor_y >= vt->scroll_top && vt->cursor_y <= vt->scroll_bottom) {
                int moveLines = vt->scroll_bottom - vt->cursor_y - n + 1;
                if (moveLines > 0) {
                    memmove(&vt->screen[vt->cursor_y * vt->cols],
                            &vt->screen[(vt->cursor_y + n) * vt->cols],
                            moveLines * vt->cols);
                    memmove(&vt->attrs[vt->cursor_y * vt->cols],
                            &vt->attrs[(vt->cursor_y + n) * vt->cols],
                            moveLines * vt->cols * sizeof(vt100_attr_t));
                }
                int clearStart = VT100_MAX(vt->cursor_y, vt->scroll_bottom - n + 1);
                for (int row = clearStart; row <= vt->scroll_bottom; row++) {
                    for (int col = 0; col < vt->cols; col++) {
                        setChar(vt, col, row, ' ');
                    }
                }
            }
            break;
        }

        case '@': {
            int n = VT100_MIN((params[0] > 0) ? params[0] : 1, vt->cols - vt->cursor_x);
            for (int x = vt->cols - 1; x >= vt->cursor_x + n; x--) {
                int src = xyToIndex(vt, x - n, vt->cursor_y);
                int dst = xyToIndex(vt, x, vt->cursor_y);
                vt->screen[dst] = vt->screen[src];
                vt->attrs[dst] = vt->attrs[src];
            }
            for (int x = vt->cursor_x; x < vt->cursor_x + n; x++) {
                setChar(vt, x, vt->cursor_y, ' ');
            }
            break;
        }

        case 'P': {
            int n = VT100_MIN((params[0] > 0) ? params[0] : 1, vt->cols - vt->cursor_x);
            for (int x = vt->cursor_x; x < vt->cols - n; x++) {
                int src = xyToIndex(vt, x + n, vt->cursor_y);
                int dst = xyToIndex(vt, x, vt->cursor_y);
                vt->screen[dst] = vt->screen[src];
                vt->attrs[dst] = vt->attrs[src];
            }
            for (int x = vt->cols - n; x < vt->cols; x++) {
                setChar(vt, x, vt->cursor_y, ' ');
            }
            break;
        }

        case 'X': {
            int n = (params[0] > 0) ? params[0] : 1;
            for (int i = 0; i < n && vt->cursor_x + i < vt->cols; i++) {
                setChar(vt, vt->cursor_x + i, vt->cursor_y, ' ');
            }
            break;
        }

        case 'x':
            if (vt->write_callback) {
                int sol = ((paramCount > 0 && params[0] == 1) ? 1 : 0) + 2;
                char response[32];
                snprintf(response, sizeof(response), "\033[%d;1;1;128;128;1;0x", sol);
                vt->write_callback(response, strlen(response));
            }
            break;

        case 'c':
            if (vt->write_callback) {
                if (vt->flag == '>') {
                    const char *response = "\033[>6;20;0c";
                    vt->write_callback(response, strlen(response));
                } else if (vt->flag != '?') {
                    const char *response = "\033[?6c";
                    vt->write_callback(response, strlen(response));
                }
            }
            break;

        case 'h':
        case 'l':
            if (vt->flag == '?') {
                if (paramCount > 0) {
                    for (int i = 0; i < paramCount; i++) {
                        if (params[i] == 1) {
                            vt->app_cursor_keys = (command == 'h');
                        } else if (params[i] == 5) {
                            vt->screen_reverse = (command == 'h');
                        } else if (params[i] == 6) {
                            vt->origin_mode = (command == 'h');
                            setCursor(vt, 0, 0);
                        } else if (params[i] == 7) {
                            vt->auto_wrap = (command == 'h');
                        } else if (params[i] == 25) {
                            vt->cursor_visible = (command == 'h');
                        }
                    }
                }
            } else {
                if (paramCount > 0) {
                    for (int i = 0; i < paramCount; i++) {
                        if (params[i] == 4) {
                            vt->insert_mode = (command == 'h');
                        } else if (params[i] == 20) {
                            vt->line_feed_mode = (command == 'h') ? 1 : 0;
                        }
                    }
                }
            }
            break;

        case 's':
            vt->saved_cursor_x = vt->cursor_x;
            vt->saved_cursor_y = vt->cursor_y;
            vt->saved_attr = vt->current_attr;
            vt->saved_graphics_mode = vt->graphics_mode;
            vt->saved_origin_mode = vt->origin_mode;
            break;

        case 'u':
            vt->cursor_x = vt->saved_cursor_x;
            vt->cursor_y = vt->saved_cursor_y;
            vt->current_attr = vt->saved_attr;
            vt->graphics_mode = vt->saved_graphics_mode;
            vt->origin_mode = vt->saved_origin_mode;
            break;

        case 'g':
            if (params[0] == 0) {
                if (vt->cursor_x < TERM_MAX_COLS) {
                    vt->tab_stops[vt->cursor_x] = 0;
                }
            } else if (params[0] == 3) {
                memset(vt->tab_stops, 0, sizeof(vt->tab_stops));
            }
            break;

        case 'n':
            if (params[0] == 5) {
                if (vt->write_callback) {
                    const char *response = "\033[0n";
                    vt->write_callback(response, strlen(response));
                }
            } else if (params[0] == 6) {
                int reportY = vt->cursor_y;
                int reportX = vt->cursor_x;
                if (vt->origin_mode) {
                    reportY = vt->cursor_y - vt->scroll_top;
                }
                if (vt->write_callback) {
                    char response[32];
                    snprintf(response, sizeof(response), "\033[%d;%dR", reportY + 1, reportX + 1);
                    vt->write_callback(response, strlen(response));
                }
            }
            break;

        case 't':
            if (params[0] == 18) {
                if (vt->write_callback) {
                    char response[32];
                    snprintf(response, sizeof(response), "\033[8;%d;%dt", vt->rows, vt->cols);
                    vt->write_callback(response, strlen(response));
                }
            } else {
                if (vt->write_callback) {
                    const char *response = "\033[0t";
                    vt->write_callback(response, strlen(response));
                }
            }
            break;

        default:
            break;
    }
}

static void handleEscape(vt100_t *vt, char c) {
    switch (c) {
        case 'M':
            if (vt->cursor_y > vt->scroll_top) {
                vt->cursor_y--;
            } else {
                scrollDown(vt);
            }
            break;

        case 'D':
            newline(vt);
            break;

        case 'E':
            vt->cursor_x = 0;
            newline(vt);
            break;

        case '7':
            vt->saved_cursor_x = vt->cursor_x;
            vt->saved_cursor_y = vt->cursor_y;
            vt->saved_attr = vt->current_attr;
            vt->saved_graphics_mode = vt->graphics_mode;
            vt->saved_origin_mode = vt->origin_mode;
            break;

        case '8':
            vt->cursor_x = vt->saved_cursor_x;
            vt->cursor_y = vt->saved_cursor_y;
            vt->current_attr = vt->saved_attr;
            vt->graphics_mode = vt->saved_graphics_mode;
            vt->origin_mode = vt->saved_origin_mode;
            break;

        case 'c':
            vt->origin_mode = 0;
            vt->line_feed_mode = 0;
            vt->auto_wrap = 1;
            vt->screen_reverse = 0;
            vt->app_cursor_keys = 0;
            vt->cursor_visible = 1;
            vt->insert_mode = 0;
            vt->graphics_mode = 0;
            vt->scroll_top = 0;
            vt->scroll_bottom = vt->rows - 1;
            vt->current_attr = (vt100_attr_t)vt100_attr_init();
            vt->saved_cursor_x = 0;
            vt->saved_cursor_y = 0;
            vt->saved_attr = (vt100_attr_t)vt100_attr_init();
            vt->saved_graphics_mode = 0;
            vt->saved_origin_mode = 0;
            vt100_clear_screen(vt);
            for (int i = 0; i < TERM_MAX_COLS; i++) {
                vt->tab_stops[i] = (i % 8 == 0 && i > 0) ? 1 : 0;
            }
            break;

        case 'H':
            if (vt->cursor_x < TERM_MAX_COLS) {
                vt->tab_stops[vt->cursor_x] = 1;
            }
            break;

        default:
            break;
    }
}

void vt100_process(vt100_t *vt, char c) {
    vt->needs_redraw = 1;

    switch (vt->state) {
        case VT100_STATE_GROUND:
            if (c == '\033') {
                vt->utf8_remaining = 0;
                vt->state = VT100_STATE_ESCAPE;
                vt->escape_pos = 0;
                vt->escape_buf[vt->escape_pos++] = c;
            } else if ((unsigned char)c >= 0xF0) {
                vt->utf8_codepoint = (unsigned char)c & 0x07;
                vt->utf8_remaining = 3;
            } else if ((unsigned char)c >= 0xE0) {
                vt->utf8_codepoint = (unsigned char)c & 0x0F;
                vt->utf8_remaining = 2;
            } else if ((unsigned char)c >= 0xC0) {
                vt->utf8_codepoint = (unsigned char)c & 0x1F;
                vt->utf8_remaining = 1;
            } else if ((unsigned char)c >= 0x80 && vt->utf8_remaining > 0) {
                vt->utf8_codepoint = (vt->utf8_codepoint << 6) | ((unsigned char)c & 0x3F);
                vt->utf8_remaining--;
                if (vt->utf8_remaining == 0) {
                    handleCodepoint(vt, vt->utf8_codepoint);
                }
            } else {
                vt->utf8_remaining = 0;
                handleChar(vt, c);
            }
            break;

        case VT100_STATE_ESCAPE:
            if (vt->escape_pos < 31) {
                vt->escape_buf[vt->escape_pos++] = c;
            }
            if (c == '[') {
                vt->state = VT100_STATE_CSI;
            } else if (c == ']') {
                vt->state = VT100_STATE_OSC;
            } else if (c == '(' || c == ')') {
                vt->state = VT100_STATE_CHARSET;
            } else if (c >= ' ' && c <= '~') {
                handleEscape(vt, c);
                vt->state = VT100_STATE_GROUND;
            }
            break;

        case VT100_STATE_CHARSET:
            if (c == '0') {
                vt->graphics_mode = 1;
            } else {
                vt->graphics_mode = 0;
            }
            vt->state = VT100_STATE_GROUND;
            break;

        case VT100_STATE_CSI:
            if (c == '\030' || c == '\032') {
                vt->state = VT100_STATE_GROUND;
            } else if (c == '\033') {
                vt->state = VT100_STATE_ESCAPE;
                vt->escape_pos = 0;
                vt->escape_buf[vt->escape_pos++] = c;
            } else if (c < 0x20) {
                handleChar(vt, c);
            } else {
                if (vt->escape_pos < 31) {
                    vt->escape_buf[vt->escape_pos++] = c;
                }
                if (c >= '@' && c <= '~') {
                    executeCSI(vt, vt->escape_buf, vt->escape_pos);
                    vt->state = VT100_STATE_GROUND;
                }
            }
            break;

        case VT100_STATE_OSC:
            if (c == '\033' || c == '\007') {
                if (vt->escape_pos > 2 && vt->escape_buf[2] == ';') {
                    int titleStart = 3;
                    for (int i = titleStart; i < vt->escape_pos; i++) {
                        if (vt->escape_buf[i] == '\033' || vt->escape_buf[i] == '\007') {
                            vt->escape_buf[i] = '\0';
                            break;
                        }
                    }
                    char title[64];
                    strncpy(title, vt->escape_buf + titleStart, sizeof(title) - 1);
                    title[sizeof(title) - 1] = '\0';
                    if (vt->title_callback) {
                        vt->title_callback(title);
                    }
                }
                vt->state = VT100_STATE_GROUND;
            } else if (vt->escape_pos < 31) {
                vt->escape_buf[vt->escape_pos++] = c;
            }
            break;
    }
}

char vt100_get_char(const vt100_t *vt, int x, int y) {
    int idx = xyToIndex(vt, x, y);
    if (idx >= 0 && idx < vt->buffer_size) {
        return vt->screen[idx];
    }
    return ' ';
}

vt100_attr_t vt100_get_attr(const vt100_t *vt, int x, int y) {
    int idx = xyToIndex(vt, x, y);
    if (idx >= 0 && idx < vt->buffer_size) {
        return vt->attrs[idx];
    }
    return (vt100_attr_t)vt100_attr_init();
}

char vt100_get_char_with_origin(const vt100_t *vt, int x, int y) {
    applyOriginMode(vt, &x, &y);
    return vt100_get_char(vt, x, y);
}

vt100_attr_t vt100_get_attr_with_origin(const vt100_t *vt, int x, int y) {
    applyOriginMode(vt, &x, &y);
    return vt100_get_attr(vt, x, y);
}

int vt100_cursor_x(const vt100_t *vt) {
    return vt->cursor_x;
}

int vt100_cursor_y(const vt100_t *vt) {
    return vt->cursor_y;
}

int vt100_needs_redraw(const vt100_t *vt) {
    return vt->needs_redraw;
}

void vt100_clear_redraw_flag(vt100_t *vt) {
    vt->needs_redraw = 0;
}

void vt100_clear_screen(vt100_t *vt) {
    memset(vt->screen, ' ', (size_t)vt->buffer_size);
    for (int i = 0; i < vt->buffer_size; i++) {
        vt->attrs[i] = vt->current_attr;
    }
    setCursor(vt, 0, 0);
}

int vt100_cols(const vt100_t *vt) {
    return vt->cols;
}

int vt100_rows(const vt100_t *vt) {
    return vt->rows;
}

int vt100_line_feed_mode(const vt100_t *vt) {
    return vt->line_feed_mode;
}

int vt100_screen_reverse(const vt100_t *vt) {
    return vt->screen_reverse;
}

int vt100_app_cursor_keys(const vt100_t *vt) {
    return vt->app_cursor_keys;
}

int vt100_cursor_visible(const vt100_t *vt) {
    return vt->cursor_visible;
}

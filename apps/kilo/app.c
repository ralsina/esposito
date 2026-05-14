/* Kilo editor adapted for esposito OS
 * Based on Salvatore Sanfilippo's original kilo editor
 * Adapted to use terminal_mode API and ESP32 file system
 */

#include "os_core.h"
#include "app_config.h"
#include "terminal_mode.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

static const char *TAG = "kilo";

#define KILO_VERSION "0.0.1"

/* Syntax highlight types */
#define HL_NORMAL 0
#define HL_NONPRINT 1
#define HL_COMMENT 2
#define HL_MLCOMMENT 3
#define HL_KEYWORD1 4
#define HL_KEYWORD2 5
#define HL_STRING 6
#define HL_NUMBER 7
#define HL_MATCH 8

#define HL_HIGHLIGHT_STRINGS (1<<0)
#define HL_HIGHLIGHT_NUMBERS (1<<1)

/* Default terminal dimensions for esposito */
#define DEFAULT_COLS 64
#define DEFAULT_ROWS 29

/* Row represents a single line of the file being edited */
typedef struct erow {
    int idx;
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *hl;
    int hl_oc;
} erow;

/* Editor configuration and state */
typedef struct {
    int cx, cy;
    int rowoff, coloff;
    int screenrows, screencols;
    int numrows;
    erow *row;
    int dirty;
    char filename[256];
    char statusmsg[512];
    time_t statusmsg_time;
    int prompt_mode;
    char prompt_input[256];
    int prompt_len;
} editor_t;

typedef struct {
    terminal_mode_t *term;
    editor_t editor;
} kilo_context_t;

static kilo_context_t *kilo = NULL;

#define PROMPT_NONE 0
#define PROMPT_OPEN 1
#define PROMPT_SAVE_AS 2

/* ======================== Syntax Highlighting ======================== */

const char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
const char *C_HL_keywords[] = {
    "switch", "while", "for", "forach", "do", "break", "continue", "return",
    "if", "else", "struct", "union", "typedef", "static", "enum", "class",
    "case",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", NULL
};

/* ======================== Editor Core ======================== */

static erow *editorRowAt(editor_t *editor, int index) {
    if (index < 0 || index >= editor->numrows) return NULL;
    return &editor->row[index];
}

static int editorRowSize(erow *row) {
    return row ? row->size : 0;
}

static void editorUpdateRow(erow *row) {
    free(row->render);
    free(row->hl);
    row->render = malloc(row->size * 4 + 1);
    row->hl = malloc(row->size);
    int idx = 0;
    for (int i = 0; i < row->size; i++) {
        if (row->chars[i] == '\t') {
            row->render[idx++] = ' ';
            while ((idx + 1) % 8 != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[i];
        }
        row->hl[i] = HL_NORMAL;
    }
    row->rsize = idx;
    row->render[row->rsize] = '\0';
}

static void editorInsertRow(editor_t *editor, int at, const char *s, size_t len) {
    if (at < 0 || at > editor->numrows) return;
    
    editor->row = realloc(editor->row, sizeof(erow) * (editor->numrows + 1));
    memmove(&editor->row[at + 1], &editor->row[at], sizeof(erow) * (editor->numrows - at));
    
    erow *new_row = &editor->row[at];
    new_row->size = len;
    new_row->chars = malloc(len + 1);
    memcpy(new_row->chars, s, len);
    new_row->chars[len] = '\0';
    new_row->render = NULL;
    new_row->hl = NULL;
    new_row->hl_oc = 0;
    editorUpdateRow(new_row);
    
    for (int i = at; i <= editor->numrows; i++) {
        editor->row[i].idx = i;
    }
    editor->numrows++;
    editor->dirty = 1;
}

static void editorDelRow(editor_t *editor, int at) {
    if (at < 0 || at >= editor->numrows) return;
    erow *row = &editor->row[at];
    free(row->chars);
    free(row->render);
    free(row->hl);
    memmove(&editor->row[at], &editor->row[at + 1], sizeof(erow) * (editor->numrows - at - 1));
    editor->numrows--;
    editor->dirty = 1;
}

static void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
}

static void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
}

static void editorRowAppendString(erow *row, const char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
}

static void editorInsertNewline(editor_t *editor) {
    erow *row = editorRowAt(editor, editor->cy);
    if (!row) {
        editorInsertRow(editor, editor->numrows, "", 0);
        return;
    }
    
    editorInsertRow(editor, editor->cy + 1, &row->chars[editor->cx], row->size - editor->cx);
    row = editorRowAt(editor, editor->cy);
    row->size = editor->cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    editor->cy++;
    editor->cx = 0;
}

static void editorInsertChar(editor_t *editor, int c) {
    if (editor->cy == editor->numrows) {
        editorInsertRow(editor, editor->numrows, "", 0);
    }
    erow *row = editorRowAt(editor, editor->cy);
    editorRowInsertChar(row, editor->cx, c);
    editor->cx++;
}

static void editorDelChar(editor_t *editor) {
    erow *row = editorRowAt(editor, editor->cy);
    if (!row || (editor->cx == 0 && editor->cy == 0)) return;
    
    if (editor->cx > 0) {
        editorRowDelChar(row, editor->cx - 1);
        editor->cx--;
    } else {
        erow *prev_row = editorRowAt(editor, editor->cy - 1);
        editor->cx = prev_row->size;
        editorRowAppendString(prev_row, row->chars, row->size);
        editorDelRow(editor, editor->cy);
        editor->cy--;
    }
}

static void editorFreeRows(editor_t *editor) {
    if (!editor || !editor->row) return;
    for (int i = 0; i < editor->numrows; i++) {
        free(editor->row[i].chars);
        free(editor->row[i].render);
        free(editor->row[i].hl);
    }
    free(editor->row);
    editor->row = NULL;
    editor->numrows = 0;
}

static void editorSetStatus(editor_t *editor, const char *msg) {
    if (!editor) return;
    if (!msg) msg = "";
    strncpy(editor->statusmsg, msg, sizeof(editor->statusmsg) - 1);
    editor->statusmsg[sizeof(editor->statusmsg) - 1] = '\0';
    editor->statusmsg_time = time(NULL);
}

static void editorResolvePath(const char *input, char *output, size_t out_size) {
    if (!input || !input[0]) {
        snprintf(output, out_size, "/sdcard/apps/kilo/untitled.txt");
        return;
    }

    if (input[0] == '/') {
        snprintf(output, out_size, "%s", input);
        return;
    }

    snprintf(output, out_size, "/sdcard/apps/kilo/%s", input);
}

/* ======================== File I/O ======================== */

static int editorOpen(editor_t *editor, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return 0;

    editorFreeRows(editor);
    
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        int len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            len--;
        }
        editorInsertRow(editor, editor->numrows, line, len);
    }
    fclose(fp);
    editor->dirty = 0;
    strncpy(editor->filename, filename, sizeof(editor->filename) - 1);
    editor->filename[sizeof(editor->filename) - 1] = '\0';
    editor->cx = 0;
    editor->cy = 0;
    editor->rowoff = 0;
    editor->coloff = 0;
    return 1;
}

static int editorSave(editor_t *editor) {
    if (!editor->filename[0]) {
        snprintf(editor->statusmsg, sizeof(editor->statusmsg),
                 "No filename set");
        return 0;  /* Not saved yet */
    }
    
    FILE *fp = fopen(editor->filename, "w");
    if (!fp) {
        os_log(TAG, "Save open failed: %s", editor->filename);
        return 0;
    }
    
    for (int i = 0; i < editor->numrows; i++) {
        fwrite(editor->row[i].chars, 1, editor->row[i].size, fp);
        fputc('\n', fp);
    }
    fclose(fp);
    editor->dirty = 0;
    os_log(TAG, "Saved file: %s", editor->filename);
    return 1;
}

/* Save with a provided filename */
static int editorSaveAs(editor_t *editor, const char *filename) {
    if (!filename || !filename[0]) {
        strncpy(editor->filename, "/sdcard/apps/kilo/untitled.txt", sizeof(editor->filename) - 1);
    } else {
        strncpy(editor->filename, filename, sizeof(editor->filename) - 1);
    }
    editor->filename[sizeof(editor->filename) - 1] = '\0';
    return editorSave(editor);
}

static void editorPromptStart(editor_t *editor, int mode) {
    if (!editor) return;
    editor->prompt_mode = mode;
    editor->prompt_len = 0;
    editor->prompt_input[0] = '\0';
}

static int editorPromptHandleKey(editor_t *editor, char key) {
    if (!editor || editor->prompt_mode == PROMPT_NONE) return 0;

    if (key == 27) {
        editor->prompt_mode = PROMPT_NONE;
        editorSetStatus(editor, "Canceled");
        return 1;
    }

    if (key == '\b' || key == 127) {
        if (editor->prompt_len > 0) {
            editor->prompt_len--;
            editor->prompt_input[editor->prompt_len] = '\0';
        }
        return 1;
    }

    if (key == '\n' || key == '\r') {
        char path[256];
        editorResolvePath(editor->prompt_input, path, sizeof(path));

        if (editor->prompt_mode == PROMPT_OPEN) {
            if (editorOpen(editor, path)) {
                editorSetStatus(editor, "Opened file");
            } else {
                editorSetStatus(editor, "Open failed");
                os_log(TAG, "Open failed: %s", path);
            }
        } else if (editor->prompt_mode == PROMPT_SAVE_AS) {
            if (editorSaveAs(editor, path)) {
                editorSetStatus(editor, "Saved file");
            } else {
                editorSetStatus(editor, "Save failed");
            }
        }

        editor->prompt_mode = PROMPT_NONE;
        return 1;
    }

    if (key >= 32 && key < 127 && editor->prompt_len < (int)sizeof(editor->prompt_input) - 1) {
        editor->prompt_input[editor->prompt_len++] = key;
        editor->prompt_input[editor->prompt_len] = '\0';
        return 1;
    }

    return 1;
}

/* ======================== Rendering ======================== */

static void editorRefresh(editor_t *editor, terminal_mode_t *term) {
    if (editor->cy < editor->rowoff) editor->rowoff = editor->cy;
    if (editor->cy >= editor->rowoff + editor->screenrows) {
        editor->rowoff = editor->cy - editor->screenrows + 1;
    }
    if (editor->cx < editor->coloff) editor->coloff = editor->cx;
    if (editor->cx >= editor->coloff + editor->screencols) {
        editor->coloff = editor->cx - editor->screencols + 1;
    }
    
    /* Build output buffer with VT100 commands */
    char buf[4096];
    int len = 0;
    
    /* Home cursor */
    len += snprintf(buf + len, sizeof(buf) - len, "\x1b[H");
    
    /* Draw visible rows */
    for (int row = 0; row < editor->screenrows; row++) {
        int filerow = row + editor->rowoff;
        
        /* Position cursor at start of line */
        len += snprintf(buf + len, sizeof(buf) - len, "\x1b[%d;1H", row + 1);
        
        if (filerow >= editor->numrows) {
            /* Empty line in file view */
            len += snprintf(buf + len, sizeof(buf) - len, "~");
        } else {
            erow *er = &editor->row[filerow];
            int col = 0;
            int display_col = 0;
            
            /* Output line content starting from coloff */
            while (col < er->rsize && display_col < editor->screencols) {
                if (col >= editor->coloff) {
                    len += snprintf(buf + len, sizeof(buf) - len, "%c", er->render[col]);
                    display_col++;
                }
                col++;
            }
            
            /* Pad to end of line with spaces */
            while (display_col < editor->screencols) {
                len += snprintf(buf + len, sizeof(buf) - len, " ");
                display_col++;
            }
        }
    }
    
    /* Position cursor at current location */
    int display_row = editor->cy - editor->rowoff;
    int display_col = editor->cx - editor->coloff;
    len += snprintf(buf + len, sizeof(buf) - len, "\x1b[%d;%dH", display_row + 1, display_col + 1);
    
    /* Feed output to terminal mode */
    terminal_mode_process_bytes(term, buf, len);
    
    /* Status message */
    char status[256];
    if (editor->prompt_mode == PROMPT_OPEN) {
        snprintf(status, sizeof(status), "Open: %s", editor->prompt_input);
    } else if (editor->prompt_mode == PROMPT_SAVE_AS) {
        snprintf(status, sizeof(status), "Save as: %s", editor->prompt_input);
    } else if (editor->statusmsg[0] && (time(NULL) - editor->statusmsg_time) < 4) {
        snprintf(status, sizeof(status), "%s", editor->statusmsg);
    } else {
        snprintf(status, sizeof(status), "%s %s | %d:%d | C-S save C-W save-as C-O open",
                 editor->dirty ? "MODIFIED" : "",
                 editor->filename[0] ? editor->filename : "[No Name]",
                 editor->cy + 1, editor->cx + 1);
    }
    terminal_mode_set_status(term, status);
    
    terminal_mode_render(term);
}

/* ======================== Input Handling ======================== */

static void editorMoveCursor(editor_t *editor, int key) {
    erow *row = editorRowAt(editor, editor->cy);
    
    switch (key) {
        case 'a':
        case 'A':
            if (editor->cx > 0) editor->cx--;
            break;
        case 'd':
        case 'D':
            if (row && editor->cx < row->size) editor->cx++;
            break;
        case 'w':
        case 'W':
            if (editor->cy > 0) editor->cy--;
            break;
        case 's':
        case 'S':
            if (editor->cy < editor->numrows - 1) editor->cy++;
            break;
    }
    
    row = editorRowAt(editor, editor->cy);
    int rowlen = editorRowSize(row);
    if (editor->cx > rowlen) editor->cx = rowlen;
}

/* ======================== App Lifecycle ======================== */

void app_init(app_context_t *ctx) {
    os_log(TAG, "Kilo editor initializing");
    
    kilo = malloc(sizeof(kilo_context_t));
    if (!kilo) {
        os_log(TAG, "Failed to allocate kilo context");
        return;
    }
    
    memset(kilo, 0, sizeof(kilo_context_t));
    
    ctx->subscriptions = EVENT_KEYBOARD;
    ctx->timer_interval_ms = 0;
    ctx->user_data = kilo;
    
    /* Initialize editor with safe defaults */
    kilo->editor.numrows = 0;
    kilo->editor.row = NULL;
    kilo->editor.cx = 0;
    kilo->editor.cy = 0;
    kilo->editor.rowoff = 0;
    kilo->editor.coloff = 0;
    kilo->editor.dirty = 0;
    kilo->editor.filename[0] = '\0';
    
    kilo->term = terminal_mode_default();
    if (!kilo->term) {
        os_log(TAG, "Failed to get terminal mode");
        free(kilo);
        kilo = NULL;
        return;
    }
    
    kilo->editor.screenrows = DEFAULT_ROWS - 2;
    kilo->editor.screencols = DEFAULT_COLS;
    
    terminal_mode_init(kilo->term, DEFAULT_COLS, DEFAULT_ROWS, NULL);
    
    /* Try to open a file from config, or start empty */
    char filename[256] = {0};
    config_bind_app("kilo");
    config_get_string("editor/file", "", filename, sizeof(filename));
    config_unbind_app();
    
    if (filename[0]) {
        if (!editorOpen(&kilo->editor, filename)) {
            os_log(TAG, "Failed to open %s", filename);
        }
    }
    
    editorRefresh(&kilo->editor, kilo->term);
}

void app_event(app_context_t *ctx, event_t *event) {
    if (!kilo) return;
    
    switch (event->type) {
        case EVENT_KEYBOARD: {
            char key = event->keyboard.key;
            uint8_t modifiers = event->keyboard.modifiers;
            uint8_t raw_key = event->keyboard.raw_key_code;

            if (kilo->editor.prompt_mode != PROMPT_NONE) {
                editorPromptHandleKey(&kilo->editor, key);
                editorRefresh(&kilo->editor, kilo->term);
                break;
            }

            /* Local editor policy: Fn+WASD are arrows, Fn+Q is tab. */
            if (modifiers & (MODIFIER_FN | MODIFIER_FN2)) {
                if (key == 'w' || key == 'W') {
                    editorMoveCursor(&kilo->editor, 'w');
                    break;
                } else if (key == 's' || key == 'S') {
                    editorMoveCursor(&kilo->editor, 's');
                    break;
                } else if (key == 'a' || key == 'A') {
                    editorMoveCursor(&kilo->editor, 'a');
                    break;
                } else if (key == 'd' || key == 'D') {
                    editorMoveCursor(&kilo->editor, 'd');
                    break;
                } else if (key == 'q' || key == 'Q') {
                    /* Fn+Q = Tab: insert 4 spaces */
                    for (int i = 0; i < 4; i++) {
                        editorInsertChar(&kilo->editor, ' ');
                    }
                    break;
                }
            }
            
            /* Handle special keys via Ctrl modifier */
            if (modifiers & MODIFIER_CTRL) {
                if (key == 'S' || key == 's' || key == 0x13 || raw_key == 's' || raw_key == 'S') {
                    int save_ok;
                    if (!kilo->editor.filename[0]) {
                        save_ok = editorSaveAs(&kilo->editor, "/sdcard/apps/kilo/untitled.txt");
                    } else {
                        save_ok = editorSave(&kilo->editor);
                    }

                    if (save_ok) {
                        snprintf(kilo->editor.statusmsg, sizeof(kilo->editor.statusmsg),
                                 "File saved: %s", kilo->editor.filename);
                        kilo->editor.statusmsg_time = time(NULL);
                    } else {
                        snprintf(kilo->editor.statusmsg, sizeof(kilo->editor.statusmsg),
                                 "Failed to save file");
                        kilo->editor.statusmsg_time = time(NULL);
                    }
                    break;
                } else if (key == 'W' || key == 'w') {
                    editorPromptStart(&kilo->editor, PROMPT_SAVE_AS);
                    editorRefresh(&kilo->editor, kilo->term);
                    break;
                } else if (key == 'O' || key == 'o') {
                    editorPromptStart(&kilo->editor, PROMPT_OPEN);
                    editorRefresh(&kilo->editor, kilo->term);
                    break;
                }
            }
            
            /* Handle regular keys */
            if (key == '\n' || key == '\r') {
                editorInsertNewline(&kilo->editor);
            } else if (key == '\t') {
                /* Tab (if not Fn+Q): insert 4 spaces */
                for (int i = 0; i < 4; i++) {
                    editorInsertChar(&kilo->editor, ' ');
                }
            } else if (key == '\b' || key == 127) {
                editorDelChar(&kilo->editor);
            } else if (key >= 32 && key < 127) {
                editorInsertChar(&kilo->editor, key);
            }
            editorRefresh(&kilo->editor, kilo->term);
            break;
        }
        default:
            break;
    }
}

void app_checkpoint(app_context_t *ctx) {
    if (kilo && kilo->editor.filename[0]) {
        config_bind_app("kilo");
        config_set_string("editor/file", kilo->editor.filename);
        config_unbind_app();
    }
}

void app_close(app_context_t *ctx) {
    if (kilo) {
        config_bind_app("kilo");
        if (kilo->editor.filename[0]) {
            config_set_string("editor/file", kilo->editor.filename);
        }
        config_unbind_app();
        
        /* Only free rows if they were allocated */
        editorFreeRows(&kilo->editor);
        
        free(kilo);
        kilo = NULL;
    }
    
    os_log(TAG, "Kilo editor shutdown");
}

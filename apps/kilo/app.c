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
#include <time.h>

static const char *TAG = "kilo";
static const char *KILO_FILE_KEY = "editor_file";

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
#define HL_HIGHLIGHT_MARKDOWN (1<<2)

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
    struct editorSyntax *syntax;
} editor_t;

typedef struct editorSyntax {
    const char *filetype;
    const char **filematch;
    const char **keywords;
    const char *singleline_comment_start;
    const char *multiline_comment_start;
    const char *multiline_comment_end;
    int flags;
} editorSyntax;

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

const char *JSON_HL_extensions[] = {".json", NULL};
const char *JSON_HL_keywords[] = {
    "true", "false", "null",
    NULL
};

const char *MD_HL_extensions[] = {".md", ".markdown", NULL};

static editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//",
        "/*",
        "*/",
        HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS,
    },
    {
        "json",
        JSON_HL_extensions,
        JSON_HL_keywords,
        NULL,
        NULL,
        NULL,
        HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS,
    },
    {
        "markdown",
        MD_HL_extensions,
        NULL,
        NULL,
        NULL,
        NULL,
        HL_HIGHLIGHT_MARKDOWN,
    },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/* ======================== Editor Core ======================== */

static erow *editorRowAt(editor_t *editor, int index) {
    if (index < 0 || index >= editor->numrows) return NULL;
    return &editor->row[index];
}

static int editorRowSize(erow *row) {
    return row ? row->size : 0;
}

/* Convert character index (cx) to rendered column index (rx), expanding tabs. */
static int editorRowCxToRx(const erow *row, int cx) {
    if (!row) return 0;
    if (cx < 0) cx = 0;
    if (cx > row->size) cx = row->size;

    int rx = 0;
    for (int index = 0; index < cx; index++) {
        if (row->chars[index] == '\t') {
            rx += (8 - 1) - (rx % 8);
        }
        rx++;
    }
    return rx;
}

static int ascii_is_space(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int ascii_is_digit(int c) {
    return c >= '0' && c <= '9';
}

static int is_separator(int c) {
    return ascii_is_space(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

static int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_COMMENT:
        case HL_MLCOMMENT:
            return 36; /* cyan */
        case HL_KEYWORD1:
            return 33; /* yellow */
        case HL_KEYWORD2:
            return 32; /* green */
        case HL_STRING:
            return 35; /* magenta */
        case HL_NUMBER:
            return 31; /* red */
        case HL_MATCH:
            return 34; /* blue */
        default:
            return 37; /* white */
    }
}

static void editorUpdateSyntax(editor_t *editor, erow *row) {
    row->hl = realloc(row->hl, row->rsize);
    if (!row->hl) return;
    memset(row->hl, HL_NORMAL, row->rsize);

    if (!editor->syntax) return;

    if (editor->syntax->flags & HL_HIGHLIGHT_MARKDOWN) {
        int pos = 0;
        while (pos < row->rsize && row->render[pos] == ' ') {
            pos++;
        }

        if (pos < row->rsize && row->render[pos] == '#') {
            memset(&row->hl[pos], HL_KEYWORD1, row->rsize - pos);
            row->hl_oc = 0;
            return;
        }

        if (pos + 2 < row->rsize && row->render[pos] == '`' && row->render[pos + 1] == '`' && row->render[pos + 2] == '`') {
            memset(&row->hl[pos], HL_KEYWORD2, row->rsize - pos);
            row->hl_oc = 0;
            return;
        }

        if (pos + 1 < row->rsize &&
            (row->render[pos] == '-' || row->render[pos] == '*' || row->render[pos] == '+') &&
            row->render[pos + 1] == ' ') {
            row->hl[pos] = HL_KEYWORD2;
        }
    }

    const char **keywords = editor->syntax->keywords;

    const char *scs = editor->syntax->singleline_comment_start;
    const char *mcs = editor->syntax->multiline_comment_start;
    const char *mce = editor->syntax->multiline_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int prev_sep = 1;
    int in_string = 0;
    int in_comment = (row->idx > 0 && editor->row[row->idx - 1].hl_oc);

    int index = 0;
    while (index < row->rsize) {
        char current = row->render[index];
        unsigned char previous_hl = (index > 0) ? row->hl[index - 1] : HL_NORMAL;

        if (scs_len && !in_string && !in_comment) {
            if (!strncmp(&row->render[index], scs, scs_len)) {
                memset(&row->hl[index], HL_COMMENT, row->rsize - index);
                break;
            }
        }

        if (mcs_len && mce_len && !in_string) {
            if (in_comment) {
                row->hl[index] = HL_MLCOMMENT;
                if (!strncmp(&row->render[index], mce, mce_len)) {
                    memset(&row->hl[index], HL_MLCOMMENT, mce_len);
                    index += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                } else {
                    index++;
                    continue;
                }
            } else if (!strncmp(&row->render[index], mcs, mcs_len)) {
                memset(&row->hl[index], HL_MLCOMMENT, mcs_len);
                index += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        if (editor->syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row->hl[index] = HL_STRING;
                if (current == '\\' && index + 1 < row->rsize) {
                    row->hl[index + 1] = HL_STRING;
                    index += 2;
                    prev_sep = 1;
                    continue;
                }
                if (current == in_string) in_string = 0;
                index++;
                prev_sep = 1;
                continue;
            } else {
                if (current == '"' || current == '\'') {
                    in_string = current;
                    row->hl[index] = HL_STRING;
                    index++;
                    continue;
                }
            }
        }

        if (editor->syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((ascii_is_digit(current) && (prev_sep || previous_hl == HL_NUMBER)) ||
                (current == '.' && previous_hl == HL_NUMBER)) {
                row->hl[index] = HL_NUMBER;
                index++;
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep) {
            int keyword_index;
            for (keyword_index = 0; keywords && keywords[keyword_index]; keyword_index++) {
                int keyword_length = strlen(keywords[keyword_index]);
                int keyword2 = keywords[keyword_index][keyword_length - 1] == '|';
                if (keyword2) keyword_length--;

                if (!strncmp(&row->render[index], keywords[keyword_index], keyword_length) &&
                    is_separator(row->render[index + keyword_length])) {
                    memset(&row->hl[index], keyword2 ? HL_KEYWORD2 : HL_KEYWORD1, keyword_length);
                    index += keyword_length;
                    break;
                }
            }
            if (keywords && keywords[keyword_index] != NULL) {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(current);
        index++;
    }

    int changed = (row->hl_oc != in_comment);
    row->hl_oc = in_comment;
    if (changed && row->idx + 1 < editor->numrows) {
        editorUpdateSyntax(editor, &editor->row[row->idx + 1]);
    }
}

static void editorSelectSyntaxHighlight(editor_t *editor) {
    editor->syntax = NULL;
    if (!editor->filename[0]) return;

    const char *ext = strrchr(editor->filename, '.');
    size_t entry_index;
    for (entry_index = 0; entry_index < HLDB_ENTRIES; entry_index++) {
        editorSyntax *syntax = &HLDB[entry_index];
        int filematch_index = 0;
        while (syntax->filematch[filematch_index]) {
            int is_ext = syntax->filematch[filematch_index][0] == '.';
            if ((is_ext && ext && !strcmp(ext, syntax->filematch[filematch_index])) ||
                (!is_ext && strstr(editor->filename, syntax->filematch[filematch_index]))) {
                editor->syntax = syntax;

                int line_index;
                for (line_index = 0; line_index < editor->numrows; line_index++) {
                    editorUpdateSyntax(editor, &editor->row[line_index]);
                }
                return;
            }
            filematch_index++;
        }
    }
}

static void editorUpdateRow(erow *row) {
    free(row->render);
    row->render = malloc(row->size * 4 + 1);
    int idx = 0;
    for (int i = 0; i < row->size; i++) {
        if (row->chars[i] == '\t') {
            row->render[idx++] = ' ';
            while ((idx + 1) % 8 != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[i];
        }
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
    editorUpdateSyntax(editor, new_row);
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
    for (int i = at; i < editor->numrows; i++) {
        editor->row[i].idx = i;
    }
    if (at < editor->numrows) {
        editorUpdateSyntax(editor, &editor->row[at]);
    }
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
    editorUpdateSyntax(editor, row);
    editor->cy++;
    editor->cx = 0;
}

static void editorInsertChar(editor_t *editor, int c) {
    if (editor->cy == editor->numrows) {
        editorInsertRow(editor, editor->numrows, "", 0);
    }
    erow *row = editorRowAt(editor, editor->cy);
    editorRowInsertChar(row, editor->cx, c);
    editorUpdateSyntax(editor, row);
    editor->cx++;
}

static void editorDelChar(editor_t *editor) {
    erow *row = editorRowAt(editor, editor->cy);
    if (!row || (editor->cx == 0 && editor->cy == 0)) return;
    
    if (editor->cx > 0) {
        editorRowDelChar(row, editor->cx - 1);
        editorUpdateSyntax(editor, row);
        editor->cx--;
    } else {
        erow *prev_row = editorRowAt(editor, editor->cy - 1);
        editor->cx = prev_row->size;
        editorRowAppendString(prev_row, row->chars, row->size);
        editorUpdateSyntax(editor, prev_row);
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
    editorSelectSyntaxHighlight(editor);
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
    editorSelectSyntaxHighlight(editor);
    return editorSave(editor);
}

static void editorNewFile(editor_t *editor) {
    if (!editor) return;

    editorFreeRows(editor);
    editor->cx = 0;
    editor->cy = 0;
    editor->rowoff = 0;
    editor->coloff = 0;
    editor->dirty = 0;
    editor->filename[0] = '\0';
    editor->prompt_mode = PROMPT_NONE;
    editor->prompt_len = 0;
    editor->prompt_input[0] = '\0';
    editor->syntax = NULL;
    editorSetStatus(editor, "New file");
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
    erow *current_row = editorRowAt(editor, editor->cy);
    int rx = editorRowCxToRx(current_row, editor->cx);

    if (editor->cy < editor->rowoff) editor->rowoff = editor->cy;
    if (editor->cy >= editor->rowoff + editor->screenrows) {
        editor->rowoff = editor->cy - editor->screenrows + 1;
    }
    if (rx < editor->coloff) editor->coloff = rx;
    if (rx >= editor->coloff + editor->screencols) {
        editor->coloff = rx - editor->screencols + 1;
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
            int fill = 1;
            while (fill < editor->screencols) {
                len += snprintf(buf + len, sizeof(buf) - len, " ");
                fill++;
            }
        } else {
            erow *er = &editor->row[filerow];
            int col = 0;
            int display_col = 0;
            int current_color = -1;
            
            /* Output line content starting from coloff */
            while (col < er->rsize && display_col < editor->screencols) {
                if (col >= editor->coloff) {
                    int color = editorSyntaxToColor(er->hl ? er->hl[col] : HL_NORMAL);
                    if (color != current_color) {
                        current_color = color;
                        len += snprintf(buf + len, sizeof(buf) - len, "\x1b[%dm", color);
                    }
                    len += snprintf(buf + len, sizeof(buf) - len, "%c", er->render[col]);
                    display_col++;
                }
                col++;
            }

            if (current_color != -1) {
                len += snprintf(buf + len, sizeof(buf) - len, "\x1b[39m");
            }
            
            /* Pad to end of line with spaces */
            while (display_col < editor->screencols) {
                len += snprintf(buf + len, sizeof(buf) - len, " ");
                display_col++;
            }
        }
    }

    /* Shortcut help line just above terminal status bar */
    {
        const char *help = "^S save  ^W save-as  ^O open  ^N new  Fn+WASD move  Fn+Q tab";
        char help_line[DEFAULT_COLS + 1];
        int help_row = editor->screenrows + 1;
        int help_len = (int)strlen(help);
        if (help_len > editor->screencols) help_len = editor->screencols;

        memset(help_line, ' ', editor->screencols);
        help_line[editor->screencols] = '\0';
        memcpy(help_line, help, help_len);

        len += snprintf(buf + len, sizeof(buf) - len, "\x1b[%d;1H", help_row + 1);
        len += snprintf(buf + len, sizeof(buf) - len, "\x1b[90m%s\x1b[39m", help_line);
    }
    
    /* Position cursor at current location */
    int display_row = editor->cy - editor->rowoff;
    int display_col = rx - editor->coloff;
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
        const char *mode = editor->syntax ? editor->syntax->filetype : "text";
        snprintf(status, sizeof(status), "%s %s | %d:%d | %s",
                 editor->dirty ? "MODIFIED" : "",
                 editor->filename[0] ? editor->filename : "[No Name]",
                 editor->cy + 1, editor->cx + 1,
                 mode);
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
    os_consume_startup_file(filename, sizeof(filename));
    if (!filename[0] && config_bind_app("kilo")) {
        // Backward-compatible fallback for older writer paths.
        config_get_string(KILO_FILE_KEY, "", filename, sizeof(filename));
        if (!filename[0]) {
            config_get_string("editor/file", "", filename, sizeof(filename));
        }
        config_unbind_app();
    }
    
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
                    editorRefresh(&kilo->editor, kilo->term);
                    break;
                } else if (key == 's' || key == 'S') {
                    editorMoveCursor(&kilo->editor, 's');
                    editorRefresh(&kilo->editor, kilo->term);
                    break;
                } else if (key == 'a' || key == 'A') {
                    editorMoveCursor(&kilo->editor, 'a');
                    editorRefresh(&kilo->editor, kilo->term);
                    break;
                } else if (key == 'd' || key == 'D') {
                    editorMoveCursor(&kilo->editor, 'd');
                    editorRefresh(&kilo->editor, kilo->term);
                    break;
                } else if (key == 'q' || key == 'Q') {
                    /* Fn+Q = Tab: insert 4 spaces */
                    for (int i = 0; i < 4; i++) {
                        editorInsertChar(&kilo->editor, ' ');
                    }
                    editorRefresh(&kilo->editor, kilo->term);
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
                    editorRefresh(&kilo->editor, kilo->term);
                    break;
                } else if (key == 'N' || key == 'n' || key == 0x0E || raw_key == 'n' || raw_key == 'N') {
                    editorNewFile(&kilo->editor);
                    editorRefresh(&kilo->editor, kilo->term);
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
        config_set_string(KILO_FILE_KEY, kilo->editor.filename);
        config_unbind_app();
    }
}

void app_close(app_context_t *ctx) {
    if (kilo) {
        config_bind_app("kilo");
        if (kilo->editor.filename[0]) {
            config_set_string(KILO_FILE_KEY, kilo->editor.filename);
        }
        config_unbind_app();
        
        /* Only free rows if they were allocated */
        editorFreeRows(&kilo->editor);
        
        free(kilo);
        kilo = NULL;
    }
    
    os_log(TAG, "Kilo editor shutdown");
}

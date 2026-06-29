#include "commands.h"
#include "bb_io.h"
#include "bb_vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>

// Functions exported by firmware (resolved via os_symtab at runtime)
extern size_t app_heap_get_free_size(void);
extern size_t app_heap_get_total_size(void);
extern size_t app_heap_get_min_free_size(void);

static const char *g_stdin_path = NULL;

void cmd_set_stdin_file(const char *path) {
    g_stdin_path = path;
}

int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) bb_write(" ", 1);
        bb_write(argv[i], strlen(argv[i]));
    }
    bb_write("\n", 1);
    return 0;
}

int cmd_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    bb_printf("%s\n", bb_get_cwd());
    return 0;
}

int cmd_cd(int argc, char **argv) {
    if (argc < 2) {
        bb_printf("%s\n", bb_get_cwd());
        return 0;
    }
    if (bb_set_cwd(argv[1]) != 0) {
        bb_printf("cd: %s: No such directory\n", argv[1]);
        return 1;
    }
    return 0;
}

int cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    bb_write("\033[2J\033[H", 7);
    return 0;
}

int cmd_free(int argc, char **argv) {
    (void)argc; (void)argv;

    size_t free_bytes = app_heap_get_free_size();
    size_t total_bytes = app_heap_get_total_size();
    size_t min_bytes = app_heap_get_min_free_size();
    total_bytes = app_heap_get_total_size();
    min_bytes = app_heap_get_min_free_size();

    bb_printf("App heap: %uK free, %uK min, %uK total\n",
              (unsigned)(free_bytes / 1024),
              (unsigned)(min_bytes / 1024),
              (unsigned)(total_bytes / 1024));
    return 0;
}

int cmd_date(int argc, char **argv) {
    (void)argc; (void)argv;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (tm_info) {
        bb_printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                  tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                  tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }
    return 0;
}

int cmd_ls(int argc, char **argv) {
    const char *arg = argc > 1 ? argv[1] : ".";
    char path[BB_MAX_PATH];
    bb_resolve_path(arg, path, sizeof(path));

    DIR *dir = opendir(path);
    if (!dir) {
        bb_printf("ls: %s: No such directory\n", arg);
        return 1;
    }

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char full[BB_MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            bb_printf("  %s/\n", entry->d_name);
        } else {
            bb_printf("  %s\n", entry->d_name);
        }
        count++;
    }
    closedir(dir);
    bb_printf("\ntotal: %d\n", count);
    return 0;
}

static FILE *open_file_or_stdin(const char *name) {
    if (name) {
        char resolved[BB_MAX_PATH];
        bb_resolve_path(name, resolved, sizeof(resolved));
        return fopen(resolved, "r");
    }
    if (g_stdin_path) {
        return fopen(g_stdin_path, "r");
    }
    return NULL;
}

int cmd_cat(int argc, char **argv) {
    FILE *f = open_file_or_stdin(argc > 1 ? argv[1] : NULL);
    if (!f) {
        bb_printf("cat: %s: No such file\n", argc > 1 ? argv[1] : "(stdin)");
        return 1;
    }
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        bb_write(buf, strlen(buf));
    }
    fclose(f);
    return 0;
}

int cmd_head(int argc, char **argv) {
    int n = 10;
    const char *file = NULL;
    if (argc >= 3 && strcmp(argv[1], "-n") == 0) {
        n = atoi(argv[2]);
        file = argc > 3 ? argv[3] : NULL;
    } else if (argc >= 2) {
        file = argv[1];
    }

    FILE *f = open_file_or_stdin(file);
    if (!f) {
        bb_printf("head: %s: No such file\n", file ? file : "(stdin)");
        return 1;
    }
    char buf[256];
    for (int i = 0; i < n && fgets(buf, sizeof(buf), f); i++) {
        bb_write(buf, strlen(buf));
    }
    fclose(f);
    return 0;
}

int cmd_tail(int argc, char **argv) {
    int n = 10;
    const char *file = NULL;
    if (argc >= 3 && strcmp(argv[1], "-n") == 0) {
        n = atoi(argv[2]);
        file = argc > 3 ? argv[3] : NULL;
    } else if (argc >= 2) {
        file = argv[1];
    }

    FILE *f = open_file_or_stdin(file);
    if (!f) {
        bb_printf("tail: %s: No such file\n", file ? file : "(stdin)");
        return 1;
    }

    int total = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) total++;

    rewind(f);
    int skip = total > n ? total - n : 0;
    for (int i = 0; fgets(buf, sizeof(buf), f); i++) {
        if (i >= skip) bb_write(buf, strlen(buf));
    }
    fclose(f);
    return 0;
}

int cmd_more(int argc, char **argv) {
    if (argc < 2) {
        bb_printf("usage: more <file>\n");
        return 1;
    }
    char resolved[BB_MAX_PATH];
    bb_resolve_path(argv[1], resolved, sizeof(resolved));
    FILE *f = fopen(resolved, "r");
    if (!f) {
        bb_printf("more: %s: No such file\n", argv[1]);
        return 1;
    }

    int rows = 24;
    void *term = bb_get_term();
    if (term) {
        extern int terminal_mode_rows(void *t);
        int r = terminal_mode_rows(term);
        if (r > 1) rows = r - 1;
    }

    int line_count = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        bb_write(buf, strlen(buf));
        line_count++;
        if (line_count >= rows) {
            bb_write("\033[7m--More--\033[0m\n", 16);
            break;
        }
    }
    fclose(f);
    return 0;
}

int cmd_wc(int argc, char **argv) {
    const char *file = argc > 1 ? argv[1] : NULL;
    FILE *f = open_file_or_stdin(file);
    if (!f) {
        bb_printf("wc: %s: No such file\n", file ? file : "(stdin)");
        return 1;
    }

    int lines = 0, words = 0, chars = 0;
    int c, in_word = 0;
    while ((c = fgetc(f)) != EOF) {
        chars++;
        if (c == '\n') lines++;
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    fclose(f);
    bb_printf(" %d %d %d %s\n", lines, words, chars, file ? file : "");
    return 0;
}

int cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        bb_printf("usage: mkdir <dir>\n");
        return 1;
    }
    char resolved[BB_MAX_PATH];
    bb_resolve_path(argv[1], resolved, sizeof(resolved));
    if (mkdir(resolved, 0755) != 0) {
        bb_printf("mkdir: cannot create '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}

int cmd_cp(int argc, char **argv) {
    if (argc < 3) {
        bb_printf("usage: cp <src> <dst>\n");
        return 1;
    }
    char src[BB_MAX_PATH], dst[BB_MAX_PATH];
    bb_resolve_path(argv[1], src, sizeof(src));
    bb_resolve_path(argv[2], dst, sizeof(dst));

    FILE *in = fopen(src, "r");
    if (!in) {
        bb_printf("cp: cannot open '%s'\n", argv[1]);
        return 1;
    }
    FILE *out = fopen(dst, "w");
    if (!out) {
        fclose(in);
        bb_printf("cp: cannot create '%s'\n", argv[2]);
        return 1;
    }
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    return 0;
}

int cmd_mv(int argc, char **argv) {
    if (argc < 3) {
        bb_printf("usage: mv <src> <dst>\n");
        return 1;
    }
    char src[BB_MAX_PATH], dst[BB_MAX_PATH];
    bb_resolve_path(argv[1], src, sizeof(src));
    bb_resolve_path(argv[2], dst, sizeof(dst));

    if (rename(src, dst) != 0) {
        bb_printf("mv: failed\n");
        return 1;
    }
    return 0;
}

static int rm_recursive(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        // Not a directory or can't open; try to remove as file
        return remove(path);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char full[BB_MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            rm_recursive(full);
        } else {
            remove(full);
        }
    }
    closedir(dir);
    return remove(path);
}

int cmd_rm(int argc, char **argv) {
    if (argc < 2) {
        bb_printf("usage: rm [-r] <file...>\n");
        return 1;
    }

    int recursive = 0;
    int first = 1;
    if (strcmp(argv[1], "-r") == 0) {
        recursive = 1;
        first = 2;
    }
    if (first >= argc) {
        bb_printf("usage: rm [-r] <file...>\n");
        return 1;
    }

    for (int i = first; i < argc; i++) {
        char resolved[BB_MAX_PATH];
        bb_resolve_path(argv[i], resolved, sizeof(resolved));

        if (recursive) {
            rm_recursive(resolved);
        } else {
            if (remove(resolved) != 0) {
                bb_printf("rm: cannot remove '%s'\n", argv[i]);
            }
        }
    }
    return 0;
}

int cmd_df(int argc, char **argv) {
    (void)argc; (void)argv;
    struct statvfs vfs;
    if (statvfs("/sdcard", &vfs) != 0) {
        bb_printf("df: statvfs failed\n");
        return 1;
    }

    unsigned long total = (unsigned long)vfs.f_blocks * vfs.f_frsize / 1024;
    unsigned long freeb = (unsigned long)vfs.f_bfree * vfs.f_frsize / 1024;
    unsigned long used = total - freeb;

    bb_printf("Filesystem      Size   Used  Free Use%%\n");
    bb_printf("/sdcard      %5luK %5luK %5luK %3lu%%\n",
              total, used, freeb, total > 0 ? used * 100 / total : 0);
    return 0;
}

static long du_walk(const char *path) {
    long total = 0;
    DIR *dir = opendir(path);
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char full[BB_MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            total += st.st_size;
            if (S_ISDIR(st.st_mode)) {
                total += du_walk(full);
            }
        }
    }
    closedir(dir);
    return total;
}

int cmd_du(int argc, char **argv) {
    const char *arg = argc > 1 ? argv[1] : ".";
    int summary = 0;

    if (strcmp(argv[1], "-s") == 0) {
        summary = 1;
        arg = argc > 2 ? argv[2] : ".";
    }

    char path[BB_MAX_PATH];
    bb_resolve_path(arg, path, sizeof(path));

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        bb_printf("du: %s: No such directory\n", arg);
        return 1;
    }

    if (summary) {
        long total = du_walk(path);
        bb_printf("%ld\t%s\n", total / 1024, arg);
    } else {
        DIR *dir = opendir(path);
        if (!dir) return 1;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            char full[BB_MAX_PATH];
            snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
            long size = du_walk(full);
            bb_printf("%ld\t%s\n", size / 1024, entry->d_name);
        }
        closedir(dir);
        long total = du_walk(path);
        bb_printf("%ld\ttotal\n", total / 1024);
    }
    return 0;
}

int cmd_sh(int argc, char **argv) {
    if (argc < 2) {
        bb_printf("usage: sh <script>\n");
        return 1;
    }

    char resolved[BB_MAX_PATH];
    bb_resolve_path(argv[1], resolved, sizeof(resolved));

    FILE *f = fopen(resolved, "r");
    if (!f) {
        bb_printf("sh: %s: No such file\n", argv[1]);
        return 1;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        char *p = line;
        while (*p == ' ') p++;
        if (*p == '\0' || *p == '#') continue;

        extern int bb_exec(const char *cmdline);
        bb_exec(p);
    }
    fclose(f);
    return 0;
}

int cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    extern const cmd_entry_t commands[];
    extern const int command_count;

    bb_printf("Built-in commands:\n\n");
    for (int i = 0; i < command_count; i++) {
        bb_printf("  %-12s %s\n", commands[i].name, commands[i].help);
    }
    return 0;
}

const cmd_entry_t commands[] = {
    {"cat",   cmd_cat,   "Display file contents"},
    {"cd",    cmd_cd,    "Change directory"},
    {"clear", cmd_clear, "Clear screen"},
    {"cp",    cmd_cp,    "Copy file"},
    {"date",  cmd_date,  "Show date and time"},
    {"df",    cmd_df,    "Show disk free space"},
    {"du",    cmd_du,    "Show disk usage"},
    {"echo",  cmd_echo,  "Print arguments"},
    {"free",  cmd_free,  "Show memory usage"},
    {"head",  cmd_head,  "Show first lines"},
    {"help",  cmd_help,  "List commands"},
    {"ls",    cmd_ls,    "List directory contents"},
    {"mkdir", cmd_mkdir, "Create directory"},
    {"more",  cmd_more,  "Paginate file contents"},
    {"mv",    cmd_mv,    "Move/rename file"},
    {"pwd",   cmd_pwd,   "Print working directory"},
    {"rm",    cmd_rm,    "Remove file/directory"},
    {"sh",    cmd_sh,    "Run script file"},
    {"tail",  cmd_tail,  "Show last lines"},
    {"wc",    cmd_wc,    "Count lines/words/chars"},
    {NULL, NULL, NULL},
};

const int command_count = (int)(sizeof(commands) / sizeof(commands[0])) - 1;

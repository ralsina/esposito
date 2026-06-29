#include "bb_exec.h"
#include "bb_io.h"
#include "bb_vfs.h"
#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEMP_PIPE "/sdcard/.bb_pipe"

typedef struct {
    char *buffer;
    char **argv;
    int argc;
} parsed_args_t;

static int parse_args(const char *cmdline, parsed_args_t *args) {
    args->buffer = NULL;
    args->argv = NULL;
    args->argc = 0;

    if (!cmdline || !*cmdline) return 0;

    char *buf = strdup(cmdline);
    if (!buf) return -1;

    int argc = 0;
    char *p = buf;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        argc++;
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            while (*p && *p != quote) p++;
            if (*p) p++;
        } else {
            while (*p && *p != ' ') p++;
        }
    }

    if (argc == 0) { free(buf); return 0; }

    char **argv = malloc((argc + 1) * sizeof(char *));
    if (!argv) { free(buf); return -1; }

    p = buf;
    int i = 0;
    while (*p && i < argc) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            argv[i++] = p;
            while (*p && *p != quote) p++;
            if (*p) *p++ = '\0';
        } else {
            argv[i++] = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = '\0';
        }
    }
    argv[argc] = NULL;

    args->buffer = buf;
    args->argv = argv;
    args->argc = argc;
    return 0;
}

static void free_args(parsed_args_t *args) {
    if (args) {
        free(args->argv);
        free(args->buffer);
        args->argv = NULL;
        args->buffer = NULL;
        args->argc = 0;
    }
}

static int find_and_run(const char *cmdline) {
    parsed_args_t args;
    if (parse_args(cmdline, &args) != 0 || args.argc == 0) {
        return -1;
    }

    int ret = -1;
    for (int i = 0; i < command_count; i++) {
        if (strcmp(args.argv[0], commands[i].name) == 0) {
            ret = commands[i].func(args.argc, args.argv);
            break;
        }
    }

    if (ret == -1) {
        bb_printf("bb: %s: command not found\n", args.argv[0]);
    }

    free_args(&args);
    return ret == -1 ? 1 : ret;
}

static void file_write_cb(const char *data, size_t len, void *userdata) {
    FILE *f = (FILE *)userdata;
    if (f && data && len > 0) {
        fwrite(data, 1, len, f);
    }
}

static void bb_exec_with_output(const char *cmdline, const char *outfile, int append) {
    FILE *f = fopen(outfile, append ? "a" : "w");
    if (!f) {
        bb_printf("cannot redirect to %s\n", outfile);
        return;
    }

    bb_output_t out;
    out.write = file_write_cb;
    out.userdata = f;
    bb_push_output(out);

    find_and_run(cmdline);

    bb_pop_output();
    fclose(f);
}

static void bb_exec_with_input(const char *cmdline, const char *infile) {
    cmd_set_stdin_file(infile);
    find_and_run(cmdline);
    cmd_set_stdin_file(NULL);
}

int bb_exec(const char *cmdline) {
    if (!cmdline || !*cmdline) return 0;

    char *line = strdup(cmdline);
    if (!line) return -1;

    int ret = 0;

    // Pipe
    char *pipe_pos = strchr(line, '|');
    if (pipe_pos) {
        *pipe_pos = '\0';
        char *cmd1 = line;
        char *cmd2 = pipe_pos + 1;

        while (*cmd1 == ' ') cmd1++;
        while (*cmd2 == ' ') cmd2++;
        char *e1 = cmd1 + strlen(cmd1) - 1;
        char *e2 = cmd2 + strlen(cmd2) - 1;
        while (e1 > cmd1 && *e1 == ' ') *e1-- = '\0';
        while (e2 > cmd2 && *e2 == ' ') *e2-- = '\0';

        bb_exec_with_output(cmd1, TEMP_PIPE, 0);
        bb_exec_with_input(cmd2, TEMP_PIPE);
        remove(TEMP_PIPE);

        free(line);
        return ret;
    }

    // Output redirect (>> or >)
    char *cmd = line;
    char *outfile = NULL;
    int append = 0;

    char *redir_out = strstr(line, ">>");
    if (redir_out) {
        append = 1;
        *redir_out = '\0';
        outfile = redir_out + 2;
    } else {
        redir_out = strchr(line, '>');
        if (redir_out) {
            *redir_out = '\0';
            outfile = redir_out + 1;
        }
    }

    // Input redirect
    char *infile = NULL;
    char *redir_in = strchr(cmd, '<');
    if (redir_in) {
        *redir_in = '\0';
        infile = redir_in + 1;
    }

    while (*cmd == ' ') cmd++;
    char *end = cmd + strlen(cmd) - 1;
    while (end > cmd && *end == ' ') *end-- = '\0';

    if (outfile) {
        while (*outfile == ' ') outfile++;
        end = outfile + strlen(outfile) - 1;
        while (end > outfile && *end == ' ') *end-- = '\0';
        char resolved[BB_MAX_PATH];
        bb_resolve_path(outfile, resolved, sizeof(resolved));
        bb_exec_with_output(cmd, resolved, append);
        free(line);
        return ret;
    }

    if (infile) {
        while (*infile == ' ') infile++;
        end = infile + strlen(infile) - 1;
        while (end > infile && *end == ' ') *end-- = '\0';
        char resolved[BB_MAX_PATH];
        bb_resolve_path(infile, resolved, sizeof(resolved));
        bb_exec_with_input(cmd, resolved);
        free(line);
        return ret;
    }

    // No redirects
    ret = find_and_run(cmd);
    free(line);
    return ret;
}

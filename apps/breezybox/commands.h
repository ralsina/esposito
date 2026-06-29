#ifndef COMMANDS_H
#define COMMANDS_H

typedef struct {
    const char *name;
    int (*func)(int argc, char **argv);
    const char *help;
} cmd_entry_t;

extern const cmd_entry_t commands[];
extern const int command_count;

int cmd_echo(int argc, char **argv);
int cmd_pwd(int argc, char **argv);
int cmd_cd(int argc, char **argv);
int cmd_ls(int argc, char **argv);
int cmd_cat(int argc, char **argv);
int cmd_head(int argc, char **argv);
int cmd_tail(int argc, char **argv);
int cmd_more(int argc, char **argv);
int cmd_wc(int argc, char **argv);
int cmd_mkdir(int argc, char **argv);
int cmd_cp(int argc, char **argv);
int cmd_mv(int argc, char **argv);
int cmd_rm(int argc, char **argv);
int cmd_df(int argc, char **argv);
int cmd_du(int argc, char **argv);
int cmd_free(int argc, char **argv);
int cmd_date(int argc, char **argv);
int cmd_clear(int argc, char **argv);
int cmd_sh(int argc, char **argv);
int cmd_help(int argc, char **argv);

// Called by bb_exec for "read from stdin" mode
void cmd_set_stdin_file(const char *path);

#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "z80.h"
#include "spperif.h"
#include "spkey_p.h"

// sound stubs
void open_snd(void) {}
void keyboard_close(void) {}
void keyboard_getstate(void) {}
void keyboard_translatekeys(void) {}

// graphics stubs
void vga_drawscanline(void) {}
void vga_getgraphmem(void) {}
void vga_setmode(void) {}
void vga_setpalvec(void) {}

// libc stubs
void exit(int status) { (void)status; for(;;); }
int atexit(void (*f)(void)) { (void)f; return 0; }
char *tmpnam(char *s) { (void)s; return NULL; }
int fputs(const char *s, FILE *stream) { (void)s; (void)stream; return 0; }
int getchar(void) { return -1; }

// Variables referenced in spconf/disabled files
int vga_pause_bg = 0;
int load_immed = 1;

// spconf stubs
void spcf_pre_check_options(int argc, char *argv[]) { (void)argc; (void)argv; }
int spcf_read_conf_file(const char *filename) { (void)filename; return 0; }
void spcf_read_command_line(int argc, char *argv[]) { (void)argc; (void)argv; }
void spcf_read_xresources(void) {}
int spcf_find_file_type(char *filename, int *ftp, int *ftsubp) { (void)filename; (void)ftp; (void)ftsubp; return 0; }

// interf stubs
char filenamebuf[1024];
char msgbuf[2048];
int spif_can_print = 0;
char *spif_get_filename(void) { return NULL; }
char *spif_get_tape_fileinfo(int *startp, int *nump) { (void)startp; (void)nump; return NULL; }
void put_msg(const char *msg) { (void)msg; }
void put_tmp_msg(const char *msg) { (void)msg; }

// tape stubs
void spkey_textmode(void) {}
void spkey_screenmode(void) {}

// sptiming stubs
void spti_init(void) {}
void spti_sleep(unsigned long usecs) { (void)usecs; }
void spti_reset(void) {}
void spti_wait(void) {}

// snapshot stubs
int save_snapshot(const char *filename) { (void)filename; return 0; }
int load_snapshot(const char *filename) { (void)filename; return 0; }
void load_quick_snapshot(void) {}
void spscr_refresh_colors(void) {}
int save_snapshot_file_type(const char *filename, int type) { (void)filename; (void)type; return 0; }
int load_snapshot_file_type(const char *filename, int type) { (void)filename; (void)type; return 0; }

// spkey stubs (referenced by spkey.c but defined in disabled/spectkey.c / spconf.c)
qbyte sp_int_ctr = 0;
spkeyboard kb_mkey = {0};
const int need_switch_mode = 0;
int sp_nosync = 0;
int showframe = 1;
int scrmul = 1;
int privatemap = 1;

void start_play(void) {}
void start_rec(void) {}
void pause_play(void) {}
void stop_play(void) {}
int display_keyboard(void) { return 0; }
void resize_spect_scr(int mul) { (void)mul; }
void save_quick_snapshot(void) {}

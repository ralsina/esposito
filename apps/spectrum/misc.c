#include "misc.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define DIR_SEP_CHAR '/'

char *get_base_name(char *fname)
{
    char *p;
    p = fname;
    for (; *p; p++);
    for (; p >= fname && *p != DIR_SEP_CHAR; p--);
    return ++p;
}

int check_ext(const char *filename, const char *ext)
{
    int flen, elen;
    int i;

    flen = (int)strlen(filename);
    elen = (int)strlen(ext);

    if (flen <= elen + 1) return 0;

    if (filename[flen - elen - 1] != '.') return 0;
    for (i = 0; i < elen; i++)
        if (filename[flen - elen + i] != toupper((int)ext[i])) break;
    if (i == elen) return 1;
    for (i = 0; i < elen; i++)
        if (filename[flen - elen + i] != tolower((int)ext[i])) break;
    if (i == elen) return 1;
    return 0;
}

void add_extension(char *filename, const char *ext)
{
    int i;
    int upper;

    i = (int)strlen(filename);
    if (filename[i] > 64 && filename[i] < 96) upper = 1;
    else upper = 0;

    filename[i++] = '.';
    if (upper)
        for (; *ext; i++, ext++) filename[i] = toupper((int)*ext);
    else
        for (; *ext; i++, ext++) filename[i] = tolower((int)*ext);
}

int file_exist(const char *filename)
{
    FILE *fp;
    fp = fopen(filename, "rb");
    if (fp != NULL) {
        fclose(fp);
        return 1;
    }
    return 0;
}

int try_extension(char *filename, const char *ext)
{
    int tend;
    tend = (int)strlen(filename);
    add_extension(filename, ext);
    if (file_exist(filename)) return 1;
    filename[tend] = '\0';
    return 0;
}

void *malloc_err(size_t size)
{
    void *p = malloc(size);
    if (p == NULL) {
        for (;;);
    }
    return p;
}

char *make_string(char *ostr, const char *nstr)
{
    if (ostr != NULL) free(ostr);
    ostr = malloc_err(strlen(nstr) + 1);
    strcpy(ostr, nstr);
    return ostr;
}

void free_string(char *ostr)
{
    if (ostr != NULL) free(ostr);
}

int mis_strcasecmp(const char *s1, const char *s2)
{
    int c1, c2;
    for (;; s1++, s2++) {
        c1 = tolower((int)*s1);
        c2 = tolower((int)*s2);
        if (!c1 || c1 != c2) break;
    }
    return c1 - c2;
}

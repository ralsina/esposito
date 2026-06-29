#include "bb_vfs.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define BB_MOUNT_POINT "/sdcard"

static char s_cwd[BB_MAX_PATH];

void bb_vfs_init(void) {
    strcpy(s_cwd, BB_MOUNT_POINT);
}

const char *bb_get_cwd(void) {
    return s_cwd;
}

int bb_set_cwd(const char *path) {
    char new_path[BB_MAX_PATH];

    if (strcmp(path, "..") == 0) {
        if (strcmp(s_cwd, "/") == 0) return 0;
        strncpy(new_path, s_cwd, sizeof(new_path));
        new_path[sizeof(new_path) - 1] = '\0';
        char *last_slash = strrchr(new_path, '/');
        if (last_slash == new_path) {
            new_path[1] = '\0';
        } else if (last_slash) {
            *last_slash = '\0';
        }
    } else if (path[0] == '/') {
        strncpy(new_path, path, sizeof(new_path) - 1);
        new_path[sizeof(new_path) - 1] = '\0';
    } else {
        size_t cwd_len = strlen(s_cwd);
        size_t path_len = strlen(path);
        if (cwd_len + 1 + path_len >= sizeof(new_path)) return -1;

        strcpy(new_path, s_cwd);
        if (cwd_len > 1) {
            new_path[cwd_len] = '/';
            strcpy(new_path + cwd_len + 1, path);
        } else {
            strcpy(new_path + 1, path);
        }
    }

    struct stat st;
    if (strcmp(new_path, "/") == 0 ||
        (stat(new_path, &st) == 0 && S_ISDIR(st.st_mode))) {
        strcpy(s_cwd, new_path);
        return 0;
    }
    return -1;
}

static void normalize(char *path) {
    if (!path || !*path) return;
    
    // Collapse // to /
    char *src = path, *dst = path;
    while (*src) {
        *dst++ = *src;
        if (*src == '/') {
            while (*(++src) == '/');
        } else {
            src++;
        }
    }
    *dst = '\0';

    // Remove trailing /. repeatedly
    size_t len;
    while ((len = strlen(path)) >= 2 && path[len - 1] == '.' && path[len - 2] == '/') {
        path[len - 2] = '\0';
    }
    // Keep at least /
    if (path[0] == '\0') {
        path[0] = '/';
        path[1] = '\0';
    }
}

char *bb_resolve_path(const char *path, char *buf, size_t size) {
    if (path[0] == '/') {
        strncpy(buf, path, size - 1);
        buf[size - 1] = '\0';
    } else {
        size_t cwd_len = strlen(s_cwd);
        size_t path_len = strlen(path);
        if (cwd_len + 1 + path_len >= size) return NULL;

        if (cwd_len == 1 && s_cwd[0] == '/') {
            snprintf(buf, size, "/%s", path);
        } else {
            snprintf(buf, size, "%s/%s", s_cwd, path);
        }
    }
    normalize(buf);
    return buf;
}

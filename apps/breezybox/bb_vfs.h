#ifndef BB_VFS_H
#define BB_VFS_H

#include <stddef.h>

#define BB_MAX_PATH 256

void bb_vfs_init(void);
const char *bb_get_cwd(void);
int bb_set_cwd(const char *path);
char *bb_resolve_path(const char *path, char *buf, size_t size);

#endif

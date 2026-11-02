#ifndef NEUROOS_FILE_H
#define NEUROOS_FILE_H
#include "fs.h"
long nos_fs_request(struct fs_message *q, struct fs_message *r);
const char *nos_file_path(const char *path);
long nos_file_read(const char *name, unsigned offset, void *data, unsigned bytes);
long nos_file_write(const char *name, const void *data, unsigned bytes);
long nos_file_pwrite(const char *name, unsigned offset, const void *data, unsigned bytes);
long nos_file_size(const char *name);
#endif

#ifndef NEUROOS_FS_POLICY_H
#define NEUROOS_FS_POLICY_H
#include "../user/fs.h"
#include "../user/fs_scope.h"
bool fs_scope_valid(const struct nos_file_scope *scope);
bool fs_scope_allows(const struct nos_file_scope *scope, const struct fs_message *request);
bool fs_scope_create_probe(const struct nos_file_scope *scope, const struct fs_message *request);
bool fs_loader_allows(const char name[16], const struct fs_message *request);
#endif

#include "fs_policy.h"
/* Pure checks only: no IO, allocations, IPC or retained pointers. */
static bool name_valid(const char name[16]) {
    if (!name[0] || name[0] == '.')
        return false;
    bool ended = false;
    for (unsigned i = 0; i < 16; ++i) {
        char c = name[i];
        if (!c) {
            ended = true;
            continue;
        }
        if (ended || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                       c == '.' || c == '_' || c == '-'))
            return false;
    }
    return ended;
}
static bool same(const char a[16], const char b[16]) {
    for (unsigned i = 0; i < 16; ++i)
        if (a[i] != b[i])
            return false;
    return true;
}
bool fs_scope_valid(const struct nos_file_scope *scope) {
    if (!scope || !scope->count || scope->count > NOS_FILE_GRANTS || scope->reserved)
        return false;
    for (unsigned i = 0; i < NOS_FILE_GRANTS; ++i) {
        const struct nos_file_grant *g = &scope->grants[i];
        if (i >= scope->count) {
            if (g->rights)
                return false;
            for (unsigned n = 0; n < sizeof(g->name); ++n)
                if (g->name[n])
                    return false;
            continue;
        }
        if (!g->rights || (g->rights & ~NOS_FILE_RIGHTS) || !name_valid(g->name))
            return false;
        for (unsigned n = 0; n < i; ++n)
            if (same(g->name, scope->grants[n].name))
                return false;
    }
    return true;
}
bool fs_scope_allows(const struct nos_file_scope *scope, const struct fs_message *q) {
    if (!q || !fs_scope_valid(scope))
        return false;
    if (q->op == FS_BEGIN || q->op == FS_ABORT)
        return true; /* Also permits read snapshots, which must be aborted. */
    if (q->op == FS_COMMIT) {
        for (unsigned i = 0; i < scope->count; ++i)
            if (scope->grants[i].rights & (NOS_FILE_WRITE | NOS_FILE_CREATE | NOS_FILE_DELETE))
                return true;
        return false;
    }
    unsigned need = q->op == FS_READ || q->op == FS_STAT        ? NOS_FILE_READ
                    : q->op == FS_WRITE || q->op == FS_TRUNCATE ? NOS_FILE_WRITE
                    : q->op == FS_CREATE                        ? NOS_FILE_CREATE
                    : q->op == FS_DELETE                        ? NOS_FILE_DELETE
                                                                : 0;
    if (!need || !name_valid(q->name))
        return false; /* No LIST, QUIT, client-close or unknown operations. */
    for (unsigned i = 0; i < scope->count; ++i)
        if (same(q->name, scope->grants[i].name))
            return (scope->grants[i].rights & need) != 0;
    return false;
}
bool fs_loader_allows(const char name[16], const struct fs_message *q) {
    if (!name || !q)
        return false;
    return q->op == FS_BEGIN || q->op == FS_ABORT ||
           ((q->op == FS_STAT || q->op == FS_READ) && same(name, q->name));
}
bool fs_scope_create_probe(const struct nos_file_scope *scope, const struct fs_message *q) {
    if (!q || q->op != FS_CREATE || !fs_scope_valid(scope) || !name_valid(q->name))
        return false;
    for (unsigned i = 0; i < scope->count; ++i)
        if (same(q->name, scope->grants[i].name)) {
            unsigned r = scope->grants[i].rights;
            return (r & NOS_FILE_WRITE) && !(r & NOS_FILE_CREATE);
        }
    return false;
}

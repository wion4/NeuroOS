#ifndef NEUROOS_FS_SCOPE_H
#define NEUROOS_FS_SCOPE_H
#include <stdint.h>
#define NOS_FILE_GRANTS 4u
#define NOS_FILE_READ 1u
#define NOS_FILE_WRITE 2u
#define NOS_FILE_CREATE 4u
#define NOS_FILE_DELETE 8u
#define NOS_FILE_RIGHTS 15u
/* Immutable exact flat names, not directory prefixes or stable object IDs.
   Unused entries and bytes following each NUL must be zero. */
struct nos_file_grant {
    char name[16];
    uint32_t rights;
};
struct nos_file_scope {
    uint32_t count, reserved;
    struct nos_file_grant grants[NOS_FILE_GRANTS];
};
_Static_assert(sizeof(struct nos_file_grant) == 20, "File grant ABI");
_Static_assert(sizeof(struct nos_file_scope) == 88, "File scope ABI");
#endif

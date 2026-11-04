#ifndef NEUROOS_LOADER_ABI_H
#define NEUROOS_LOADER_ABI_H
#include <stdint.h>
/* Private boot-loader role only. Ordinary apps cannot replace their image or
   acquire the loader's temporary read-only FS authority. */
#define NOS_LOADER_COMMIT 0u
#define NOS_LOADER_FAIL 1u
struct nos_loader_request {
    char name[16];
};
#endif

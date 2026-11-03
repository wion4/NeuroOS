#ifndef NEUROOS_DISPLAY_H
#define NEUROOS_DISPLAY_H
#include "limine.h"
#include "paging.h"
bool display_init(struct limine_framebuffer *fb);
long display_present(const struct user_space *space, uint64_t pixels, uint64_t bytes);
#endif

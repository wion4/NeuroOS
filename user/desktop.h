#ifndef NEUROOS_DESKTOP_ABI_H
#define NEUROOS_DESKTOP_ABI_H
#include <stdint.h>
#define DESKTOP_WIDTH 800u
#define DESKTOP_HEIGHT 600u
/* Restricted to the built-in desktop. Pixels are 0x00RRGGBB. */
struct desktop_input {
    int32_t x, y;
    uint32_t buttons, key;
    uint64_t ticks;
};
#endif

#ifndef NEUROOS_KEYBOARD_H
#define NEUROOS_KEYBOARD_H
#include "desktop.h"
#include <stdbool.h>
struct keyboard_layout {
    bool russian, chord_down;
};
uint32_t keyboard_translate(struct keyboard_layout *layout, const struct desktop_input *input);
#endif

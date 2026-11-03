#ifndef NEUROOS_DESKTOP_ABI_H
#define NEUROOS_DESKTOP_ABI_H
#include <stdint.h>
#define DESKTOP_WIDTH 800u
#define DESKTOP_HEIGHT 600u
#define INPUT_SHIFT 1u
#define INPUT_CONTROL 2u
#define INPUT_ALT 4u
#define INPUT_CAPS 8u
/* Restricted to the built-in desktop. Pixels are 0x00RRGGBB. */
struct desktop_input {
    int32_t x, y;
    uint32_t buttons, key;
    uint64_t ticks;
    uint32_t modifiers, scancode;
};
#define NOS_LAUNCH_FILES 1u
enum { NOS_LAUNCH_EXITED, NOS_LAUNCH_FAULTED, NOS_LAUNCH_TIMED_OUT, NOS_LAUNCH_LOAD_ERROR };
struct desktop_launch_request {
    char name[16], arguments[80];
    uint32_t flags;
};
struct desktop_launch_result {
    uint64_t exit_code;
    uint32_t kind, fault, bytes, truncated;
    char output[2048];
};
_Static_assert(sizeof(struct desktop_input) == 32, "Desktop input ABI");
_Static_assert(sizeof(struct desktop_launch_request) == 100, "Desktop launch ABI");
_Static_assert(sizeof(struct desktop_launch_result) == 2072, "Desktop result ABI");
#endif

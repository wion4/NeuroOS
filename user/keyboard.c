#include "keyboard.h"
#include "text.h"
uint32_t keyboard_translate(struct keyboard_layout *layout, const struct desktop_input *in) {
    bool chord = (in->modifiers & (INPUT_SHIFT | INPUT_ALT)) == (INPUT_SHIFT | INPUT_ALT);
    if (chord && !layout->chord_down)
        layout->russian = !layout->russian;
    layout->chord_down = chord;
    unsigned scan = in->scancode;
    if (scan & 0x80)
        return 0;
    if (scan & 0x100) {
        switch (scan & 127) {
        case 0x48:
            return KEY_UP;
        case 0x50:
            return KEY_DOWN;
        case 0x4b:
            return KEY_LEFT;
        case 0x4d:
            return KEY_RIGHT;
        case 0x47:
            return KEY_HOME;
        case 0x4f:
            return KEY_END;
        case 0x53:
            return KEY_DELETE;
        case 0x49:
            return KEY_PAGEUP;
        case 0x51:
            return KEY_PAGEDOWN;
        default:
            return 0;
        }
    }
    if (in->modifiers & INPUT_ALT)
        return 0;
    if (in->modifiers & INPUT_CONTROL)
        return in->key;
    if (layout->russian) {
        static const uint16_t ru[128] = {
            [16] = 0x439, [17] = 0x446, [18] = 0x443, [19] = 0x43a, [20] = 0x435, [21] = 0x43d,
            [22] = 0x433, [23] = 0x448, [24] = 0x449, [25] = 0x437, [26] = 0x445, [27] = 0x44a,
            [30] = 0x444, [31] = 0x44b, [32] = 0x432, [33] = 0x430, [34] = 0x43f, [35] = 0x440,
            [36] = 0x43e, [37] = 0x43b, [38] = 0x434, [39] = 0x436, [40] = 0x44d, [41] = 0x451,
            [44] = 0x44f, [45] = 0x447, [46] = 0x441, [47] = 0x43c, [48] = 0x438, [49] = 0x442,
            [50] = 0x44c, [51] = 0x431, [52] = 0x44e};
        uint32_t cp = scan < 128 ? ru[scan] : 0;
        bool shift = (in->modifiers & INPUT_SHIFT) != 0, caps = (in->modifiers & INPUT_CAPS) != 0;
        if (cp) {
            if (shift != caps)
                cp = cp == 0x451 ? 0x401 : cp - 32;
            return cp;
        }
        if (scan == 53)
            return shift ? ',' : '.';
        if (shift && scan == 3)
            return '"';
        if (shift && scan == 4)
            return 0x2116;
        if (shift && scan == 5)
            return ';';
        if (shift && scan == 7)
            return ':';
        if (shift && scan == 8)
            return '?';
    }
    return in->key;
}

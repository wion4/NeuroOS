#include "io.h"
#include <stdbool.h>
static volatile uint64_t ticks;
static void wait_io(void) { io_out(0x80, 0); }
void timer_irq(void) {
    ++ticks;
    io_out(0x20, 0x20);
}
uint64_t timer_ticks(void) { return ticks; }
void timer_init(void) {
    __asm__ volatile("cli");
    io_out(0x20, 0x11);
    wait_io();
    io_out(0xa0, 0x11);
    wait_io();
    io_out(0x21, 0x20);
    wait_io();
    io_out(0xa1, 0x28);
    wait_io();
    io_out(0x21, 4);
    wait_io();
    io_out(0xa1, 2);
    wait_io();
    io_out(0x21, 1);
    wait_io();
    io_out(0xa1, 1);
    wait_io();
    io_out(0x21, 0xfe);
    io_out(0xa1, 0xff); /* Only timer IRQ enabled. */
    io_out(0x43, 0x36);
    io_out(0x40, 0x9c);
    io_out(0x40, 0x2e); /* divisor 11932 */
}
static bool writable(void) {
    for (unsigned i = 0; i < 100000; ++i) {
        if (!(io_in(0x64) & 2))
            return true;
        __asm__ volatile("pause");
    }
    return false;
}
static bool send(uint16_t port, uint8_t value) {
    if (!writable())
        return false;
    io_out(port, value);
    return true;
}
static int read_byte(void) {
    for (unsigned i = 0; i < 100000; ++i) {
        uint8_t status = io_in(0x64);
        if (status & 1) {
            uint8_t value = io_in(0x60);
            if (!(status & 0xe0))
                return value;
        }
        __asm__ volatile("pause");
    }
    return -1;
}
static bool keyboard_command(uint8_t value) {
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (!send(0x60, value))
            return false;
        int response = read_byte();
        if (response == 0xfa)
            return true;
        if (response != 0xfe)
            return false;
    }
    return false;
}
bool keyboard_init(void) {
    if (!send(0x64, 0xad))
        return false;
    for (unsigned i = 0; i < 256 && (io_in(0x64) & 1); ++i)
        (void)io_in(0x60);
    if (!send(0x64, 0x20))
        return false;
    int config = read_byte();
    if (config < 0)
        return false;
    /* Translated set 2 -> set 1, IRQs disabled: shell polls on timer wakeups. */
    config = (config | 0x40) & ~0x13;
    if (!send(0x64, 0x60) || !send(0x60, (uint8_t)config) || !send(0x64, 0xae))
        return false;
    return keyboard_command(0xf5) && keyboard_command(0xf0) && keyboard_command(2) &&
           keyboard_command(0xf4);
}
char keyboard_poll(void) {
    static bool left_shift, right_shift, caps;
    static bool extended;
    static unsigned pause_bytes;
    static const char keys[128] = {
        [2] = '1',  [3] = '2',  [4] = '3',   [5] = '4',  [6] = '5',   [7] = '6',   [8] = '7',
        [9] = '8',  [10] = '9', [11] = '0',  [12] = '-', [13] = '=',  [14] = '\b', [15] = '\t',
        [16] = 'q', [17] = 'w', [18] = 'e',  [19] = 'r', [20] = 't',  [21] = 'y',  [22] = 'u',
        [23] = 'i', [24] = 'o', [25] = 'p',  [26] = '[', [27] = ']',  [28] = '\n', [30] = 'a',
        [31] = 's', [32] = 'd', [33] = 'f',  [34] = 'g', [35] = 'h',  [36] = 'j',  [37] = 'k',
        [38] = 'l', [39] = ';', [40] = '\'', [41] = '`', [43] = '\\', [44] = 'z',  [45] = 'x',
        [46] = 'c', [47] = 'v', [48] = 'b',  [49] = 'n', [50] = 'm',  [51] = ',',  [52] = '.',
        [53] = '/', [57] = ' '};
    uint8_t status = io_in(0x64);
    if (!(status & 1))
        return 0;
    uint8_t code = io_in(0x60);
    if (status & 0xe0)
        return 0;
    if (pause_bytes) {
        --pause_bytes;
        return 0;
    }
    if (code == 0xe1) {
        pause_bytes = 5;
        return 0;
    }
    if (code == 0xe0) {
        extended = true;
        return 0;
    }
    if (extended) {
        extended = false;
        return 0;
    }
    if (code == 0x2a || code == 0xaa) {
        left_shift = code == 0x2a;
        return 0;
    }
    if (code == 0x36 || code == 0xb6) {
        right_shift = code == 0x36;
        return 0;
    }
    if (code == 0x3a) {
        caps = !caps;
        return 0;
    }
    if (code & 0x80)
        return 0;
    char c = keys[code];
    bool shift = left_shift || right_shift;
    if (c >= 'a' && c <= 'z')
        return shift != caps ? (char)(c - 'a' + 'A') : c;
    if (shift) {
        const char *plain = "1234567890-=[];'`\\,./";
        const char *upper = "!@#$%^&*()_+{}:\"~|<>?";
        for (unsigned i = 0; plain[i]; ++i)
            if (c == plain[i])
                return upper[i];
    }
    return c;
}

/* Tiny diagnostic bitmap font, authored for NeuroOS. No GUI or input support. */
static const uint8_t glyphs[62][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},      {14, 17, 1, 2, 4, 8, 31},
    {30, 1, 1, 14, 1, 1, 30},     {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},       {14, 17, 17, 14, 17, 17, 14},
    {14, 17, 17, 15, 1, 1, 14},   {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31},
    {31, 16, 16, 30, 16, 16, 16}, {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14},      {7, 2, 2, 2, 2, 18, 12},      {17, 18, 20, 24, 20, 18, 17},
    {16, 16, 16, 16, 16, 16, 31}, {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13},
    {30, 17, 17, 30, 20, 18, 17}, {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10},
    {17, 17, 10, 4, 10, 17, 17},  {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},
    {0, 0, 14, 1, 15, 17, 15},    {16, 16, 30, 17, 17, 17, 30}, {0, 0, 14, 16, 16, 17, 14},
    {1, 1, 15, 17, 17, 17, 15},   {0, 0, 14, 17, 31, 16, 14},   {6, 9, 8, 28, 8, 8, 8},
    {0, 0, 15, 17, 15, 1, 14},    {16, 16, 30, 17, 17, 17, 17}, {4, 0, 12, 4, 4, 4, 14},
    {2, 0, 6, 2, 2, 18, 12},      {16, 16, 18, 20, 24, 20, 18}, {12, 4, 4, 4, 4, 4, 14},
    {0, 0, 26, 21, 21, 21, 21},   {0, 0, 30, 17, 17, 17, 17},   {0, 0, 14, 17, 17, 17, 14},
    {0, 0, 30, 17, 30, 16, 16},   {0, 0, 15, 17, 15, 1, 1},     {0, 0, 22, 25, 16, 16, 16},
    {0, 0, 15, 16, 14, 1, 30},    {8, 8, 28, 8, 8, 9, 6},       {0, 0, 17, 17, 17, 19, 13},
    {0, 0, 17, 17, 17, 10, 4},    {0, 0, 17, 17, 21, 21, 10},   {0, 0, 17, 10, 4, 10, 17},
    {0, 0, 17, 17, 15, 1, 14},    {0, 0, 31, 2, 4, 8, 31}};

static void screen_pixel(struct limine_framebuffer *fb, unsigned x, unsigned y, uint32_t color) {
    if (x < fb->width && y < fb->height) {
        volatile uint32_t *row = (volatile uint32_t *)((uint8_t *)fb->address + y * fb->pitch);
        row[x] = color;
    }
}

static unsigned punctuation(char c, unsigned row) {
    switch (c) {
    case '(': {
        static const uint8_t p[7] = {2, 4, 8, 8, 8, 4, 2};
        return p[row];
    }
    case ')': {
        static const uint8_t p[7] = {8, 4, 2, 2, 2, 4, 8};
        return p[row];
    }
    case '{': {
        static const uint8_t p[7] = {3, 4, 4, 8, 4, 4, 3};
        return p[row];
    }
    case '}': {
        static const uint8_t p[7] = {24, 4, 4, 2, 4, 4, 24};
        return p[row];
    }
    case '[': {
        static const uint8_t p[7] = {14, 8, 8, 8, 8, 8, 14};
        return p[row];
    }
    case ']': {
        static const uint8_t p[7] = {14, 2, 2, 2, 2, 2, 14};
        return p[row];
    }
    case ';': {
        static const uint8_t p[7] = {0, 4, 0, 0, 4, 4, 8};
        return p[row];
    }
    case ',': {
        static const uint8_t p[7] = {0, 0, 0, 0, 4, 4, 8};
        return p[row];
    }
    case '+': {
        static const uint8_t p[7] = {0, 4, 4, 31, 4, 4, 0};
        return p[row];
    }
    case '*': {
        static const uint8_t p[7] = {0, 21, 14, 31, 14, 21, 0};
        return p[row];
    }
    case '!': {
        static const uint8_t p[7] = {4, 4, 4, 4, 4, 0, 4};
        return p[row];
    }
    case '#': {
        static const uint8_t p[7] = {10, 31, 10, 10, 31, 10, 0};
        return p[row];
    }
    case '&': {
        static const uint8_t p[7] = {12, 18, 20, 8, 21, 18, 13};
        return p[row];
    }
    case '|': {
        static const uint8_t p[7] = {4, 4, 4, 4, 4, 4, 4};
        return p[row];
    }
    case '\\': {
        static const uint8_t p[7] = {16, 8, 4, 2, 1, 0, 0};
        return p[row];
    }
    case '\'': {
        static const uint8_t p[7] = {4, 4, 8, 0, 0, 0, 0};
        return p[row];
    }
    case '"': {
        static const uint8_t p[7] = {10, 10, 0, 0, 0, 0, 0};
        return p[row];
    }
    case '?': {
        static const uint8_t p[7] = {14, 17, 1, 2, 4, 0, 4};
        return p[row];
    }
    case '<': {
        static const uint8_t p[7] = {2, 4, 8, 16, 8, 4, 2};
        return p[row];
    }
    case '%': {
        static const uint8_t p[7] = {17, 2, 4, 8, 17, 0, 0};
        return p[row];
    }
    case '^': {
        static const uint8_t p[7] = {4, 10, 17, 0, 0, 0, 0};
        return p[row];
    }
    case '~': {
        static const uint8_t p[7] = {0, 0, 9, 22, 0, 0, 0};
        return p[row];
    }
    case '@': {
        static const uint8_t p[7] = {14, 17, 23, 21, 23, 16, 14};
        return p[row];
    }
    default:
        return 0;
    }
}

static void screen_text(struct limine_framebuffer *fb, unsigned x, unsigned y, const char *text,
                        unsigned scale, uint32_t color) {
    while (*text) {
        char c = *text++;
        int index = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'A' && c <= 'Z' ? c - 'A' + 10
                    : c >= 'a' && c <= 'z' ? c - 'a' + 36
                                           : -1;
        for (unsigned row = 0; row < 7; ++row) {
            unsigned bits = index >= 0                           ? glyphs[index][row]
                            : c == '.' && row == 6               ? 4
                            : c == ':' && (row == 2 || row == 5) ? 4
                            : c == '-' && row == 3               ? 14
                            : c == '=' && (row == 2 || row == 4) ? 14
                            : c == '>'                           ? (row == 1 || row == 5   ? 8
                                                                    : row == 2 || row == 4 ? 4
                                                                    : row == 3             ? 2
                                                                                           : 0)
                            : c == '/'                           ? (row < 5 ? 1u << row : 0)
                            : c == '_' && row == 6               ? 31
                                                                 : punctuation(c, row);
            for (unsigned col = 0; col < 5; ++col)
                if (bits & (16u >> col))
                    for (unsigned dy = 0; dy < scale; ++dy)
                        for (unsigned dx = 0; dx < scale; ++dx)
                            screen_pixel(fb, x + col * scale + dx, y + row * scale + dy, color);
        }
        x += 6 * scale;
    }
}

static bool screen_boot(struct limine_framebuffer *fb, uint64_t usable_mib) {
    if (!fb || !fb->address || fb->bpp != 32 || fb->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        fb->red_mask_size != 8 || fb->green_mask_size != 8 || fb->blue_mask_size != 8 ||
        fb->red_mask_shift > 24 || fb->green_mask_shift > 24 || fb->blue_mask_shift > 24 ||
        fb->width > 8192 || fb->height > 8192 || fb->pitch < fb->width * 4 || fb->pitch > 65536)
        return false;
    uint32_t white = (220u << fb->red_mask_shift) | (230u << fb->green_mask_shift) |
                     (240u << fb->blue_mask_shift);
    uint32_t green = (70u << fb->red_mask_shift) | (220u << fb->green_mask_shift) |
                     (160u << fb->blue_mask_shift);
    for (unsigned y = 0; y < fb->height; ++y)
        for (unsigned x = 0; x < fb->width; ++x)
            screen_pixel(fb, x, y, 0);
    screen_text(fb, 32, 40, "NEUROOS V0.0.9", 3, green);
    screen_text(fb, 32, 100, "KERNEL BOOT SUCCESSFUL", 2, white);
    screen_text(fb, 32, 140, "UEFI 64 BIT", 2, white);
    screen_text(fb, 32, 180, "GDT IDT TSS READY", 2, white);
    screen_text(fb, 32, 220, "USABLE MEMORY", 2, white);
    char digits[21], text[21];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + usable_mib % 10);
        usable_mib /= 10;
    } while (usable_mib);
    unsigned n = count;
    for (unsigned i = 0; i < count; ++i)
        text[i] = digits[--n];
    text[count] = 0;
    screen_text(fb, 212, 220, text, 2, green);
    screen_text(fb, 212 + count * 12 + 12, 220, "MIB", 2, white);
    screen_text(fb, 32, 270, "CPU HALTED", 2, green);
    screen_text(fb, 32, 310, "NO SHELL OR INPUT YET", 2, white);
    return true;
}

static struct limine_framebuffer *terminal_fb;
static char terminal_cells[32][80];
static unsigned terminal_cols, terminal_rows, terminal_x, terminal_y;
static uint32_t terminal_color;
static void terminal_draw(unsigned x, unsigned y) {
    for (unsigned dy = 0; dy < 18; ++dy)
        for (unsigned dx = 0; dx < 12; ++dx)
            screen_pixel(terminal_fb, 32 + x * 12 + dx, 100 + y * 18 + dy, 0);
    char text[2] = {terminal_cells[y][x], 0};
    screen_text(terminal_fb, 32 + x * 12, 100 + y * 18, text, 2, terminal_color);
}
static void terminal_clear(void) {
    terminal_x = terminal_y = 0;
    if (!terminal_fb)
        return;
    for (unsigned y = 0; y < terminal_rows; ++y)
        for (unsigned x = 0; x < terminal_cols; ++x) {
            terminal_cells[y][x] = ' ';
            terminal_draw(x, y);
        }
}
static void terminal_init(struct limine_framebuffer *fb) {
    if (!fb || fb->width < 200 || fb->height < 200)
        return;
    terminal_fb = fb;
    terminal_cols = (unsigned)(fb->width - 64) / 12;
    terminal_rows = (unsigned)(fb->height - 120) / 18;
    if (terminal_cols > 80)
        terminal_cols = 80;
    if (terminal_rows > 32)
        terminal_rows = 32;
    terminal_color = (220u << fb->red_mask_shift) | (230u << fb->green_mask_shift) |
                     (240u << fb->blue_mask_shift);
    for (unsigned y = 85; y < fb->height; ++y)
        for (unsigned x = 0; x < fb->width; ++x)
            screen_pixel(fb, x, y, 0);
    terminal_clear();
}
static void terminal_putc(char c) {
    if (!terminal_fb)
        return;
    if (c == '\b') {
        if (terminal_x)
            --terminal_x;
        else if (terminal_y) {
            --terminal_y;
            terminal_x = terminal_cols - 1;
        }
        terminal_cells[terminal_y][terminal_x] = ' ';
        terminal_draw(terminal_x, terminal_y);
        return;
    }
    if (c == '\r') {
        terminal_x = 0;
        return;
    }
    if (c == '\n') {
        terminal_x = 0;
        ++terminal_y;
    } else {
        terminal_cells[terminal_y][terminal_x] = c;
        terminal_draw(terminal_x, terminal_y);
        if (++terminal_x >= terminal_cols) {
            terminal_x = 0;
            ++terminal_y;
        }
    }
    if (terminal_y >= terminal_rows) {
        for (unsigned y = 0; y < terminal_rows; ++y)
            for (unsigned x = 0; x < terminal_cols; ++x) {
                terminal_cells[y][x] = y + 1 < terminal_rows ? terminal_cells[y + 1][x] : ' ';
                terminal_draw(x, y);
            }
        terminal_y = terminal_rows - 1;
    }
}

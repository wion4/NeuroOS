#include "display.h"
#include "../user/desktop.h"
static struct limine_framebuffer *screen;
bool display_init(struct limine_framebuffer *fb) {
    if (!fb || !fb->address || fb->bpp != 32 || fb->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        fb->width < DESKTOP_WIDTH || fb->height < DESKTOP_HEIGHT || fb->width > 4096 ||
        fb->height > 2160 || fb->pitch < fb->width * 4 || fb->pitch > 65536 ||
        fb->red_mask_size != 8 || fb->green_mask_size != 8 || fb->blue_mask_size != 8 ||
        fb->red_mask_shift > 24 || fb->green_mask_shift > 24 || fb->blue_mask_shift > 24 ||
        fb->red_mask_shift % 8 || fb->green_mask_shift % 8 || fb->blue_mask_shift % 8 ||
        fb->red_mask_shift == fb->green_mask_shift || fb->red_mask_shift == fb->blue_mask_shift ||
        fb->green_mask_shift == fb->blue_mask_shift)
        return false;
    screen = fb;
    for (uint64_t y = 0; y < fb->height; ++y) {
        volatile uint32_t *row = (volatile uint32_t *)((uint8_t *)fb->address + y * fb->pitch);
        for (uint64_t x = 0; x < fb->width; ++x)
            row[x] = 0x080e16;
    }
    return true;
}
long display_present(const struct user_space *space, uint64_t pixels, uint64_t bytes) {
    if (!screen)
        return -19;
    if (bytes != DESKTOP_WIDTH * DESKTOP_HEIGHT * 4ull)
        return -22;
    if (pixels >= (1ull << 47) || bytes > (1ull << 47) - pixels)
        return -14;
    uint32_t row[DESKTOP_WIDTH];
    unsigned ox = (unsigned)(screen->width - DESKTOP_WIDTH) / 2;
    unsigned oy = (unsigned)(screen->height - DESKTOP_HEIGHT) / 2;
    for (unsigned y = 0; y < DESKTOP_HEIGHT; ++y) {
        if (!user_copy_from(space, row, pixels + y * sizeof(row), sizeof(row)))
            return -14;
        volatile uint32_t *out =
            (volatile uint32_t *)((uint8_t *)screen->address + (y + oy) * screen->pitch) + ox;
        for (unsigned x = 0; x < DESKTOP_WIDTH; ++x) {
            uint32_t c = row[x];
            out[x] = ((c >> 16) & 255) << screen->red_mask_shift |
                     ((c >> 8) & 255) << screen->green_mask_shift |
                     (c & 255) << screen->blue_mask_shift;
        }
    }
    return 0;
}

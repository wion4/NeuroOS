/* ATA policy in userspace; a capability-checked syscall mediates port access. */
#include "abi.h"
#include "fs.h"
#define NSECT ((512u + FS_SNAPSHOT_SIZE + 511u) / 512u)
#define FIRST_SECTOR 64u
static uint8_t image[NSECT * 512], candidate[NSECT * 512];
static uint64_t generation;
static unsigned active_bank;
static long port(unsigned op, unsigned addr, unsigned value) {
    return call3(9, 3, addr | ((uint64_t)op << 16), value);
}
static void settle(void) {
    for (unsigned i = 0; i < 4; ++i)
        (void)port(0, 0x3f6, 0);
}
static void status_error(const char *message, long status) {
    char value[] = "00\n";
    const char *digits = "0123456789ABCDEF";
    value[0] = digits[((unsigned long)status >> 4) & 15];
    value[1] = digits[(unsigned long)status & 15];
    say(message);
    say(value);
}
static bool ready(bool data) {
    uint64_t start = (uint64_t)call3(5, 0, 0, 0);
    long s = 0;
    for (unsigned i = 0; i < 1000000; ++i) {
        s = port(0, 0x1f7, 0);
        if (s < 0 || s == 0 || s == 255) {
            status_error("ATA DEVICE UNAVAILABLE STATUS ", s);
            return false;
        }
        if (!(s & 128)) {
            if (s & 0x21) {
                status_error("ATA DEVICE ERROR STATUS ", s);
                return false;
            }
            if (data ? (s & 8) : !(s & 8))
                return true;
        }
        if ((i & 255) == 255) {
            if ((uint64_t)call3(5, 0, 0, 0) - start >= 200)
                break;
            yield();
        }
    }
    status_error(data ? "ATA DATA TIMEOUT STATUS " : "ATA COMPLETION TIMEOUT STATUS ", s);
    return false;
}
static bool sector(unsigned lba, void *buffer, bool write) {
    if (port(2, 0x1f6, 0xe0) < 0)
        return false;
    settle();
    if (!ready(false))
        return false;
    if (port(2, 0x1f2, 1) < 0 || port(2, 0x1f3, lba & 255) < 0 ||
        port(2, 0x1f4, (lba >> 8) & 255) < 0 || port(2, 0x1f5, (lba >> 16) & 255) < 0 ||
        port(2, 0x1f7, write ? 0x30 : 0x20) < 0)
        return false;
    settle();
    if (!ready(true))
        return false;
    uint16_t *words = buffer;
    for (unsigned i = 0; i < 256; ++i) {
        long v = port(write ? 3 : 1, 0x1f0, write ? words[i] : 0);
        if (v < 0)
            return false;
        if (!write)
            words[i] = (uint16_t)v;
    }
    settle();
    return ready(false);
}
static bool flush(void) {
    if (port(2, 0x1f7, 0xe7) < 0)
        return false;
    settle();
    return ready(false);
}
bool storage_read_sector(uint32_t lba, void *buffer) { return sector(lba, buffer, false); }
bool storage_write_sector(uint32_t lba, const void *buffer) {
    return sector(lba, (void *)buffer, true);
}
bool storage_flush(void) { return flush(); }
static uint32_t crc(const uint8_t *p, unsigned n) {
    uint32_t c = ~0u;
    while (n--) {
        c ^= *p++;
        for (unsigned b = 0; b < 8; ++b)
            c = (c >> 1) ^ (0xedb88320u & -(c & 1));
    }
    return ~c;
}
static uint64_t get64(const uint8_t *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}
static void put64(uint8_t *p, uint64_t v) {
    for (unsigned i = 0; i < 8; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
static bool valid(const uint8_t *p, bool legacy) {
    unsigned bytes = legacy ? 8384 : FS_SNAPSHOT_SIZE;
    return get64(p) == (legacy ? 0x3153464f5255454eull : 0x3253464f5255454eull) &&
           get64(p + 16) == bytes && get64(p + 24) == crc(p + 512, bytes) &&
           get64(p + 32) == crc(p, 32);
}
int storage_load(void) {
    long device = port(0, 0x1f7, 0);
    if (device < 0 || device == 0 || device == 255)
        return -1;
    bool found = false, all_zero = true;
    for (unsigned format = 0; format < 2; ++format) {
        if (format) {
            if (!sector(63, image, false))
                return -2;
            for (unsigned i = 0; i < 512; ++i)
                if (image[i])
                    return -2; /* v2 was committed: never fall back to stale v1. */
        }
        unsigned sectors = format ? 18 : NSECT;
        unsigned first = format ? 0 : FIRST_SECTOR;
        generation = 0;
        for (unsigned bank = 0; bank < 2; ++bank) {
            bool ok = true;
            for (unsigned i = 0; i < sectors; ++i)
                if (!sector(first + bank * sectors + i, candidate + i * 512, false)) {
                    ok = false;
                    break;
                }
            if (!ok)
                return -2;
            for (unsigned i = 0; i < sectors * 512; ++i)
                if (candidate[i])
                    all_zero = false;
            if (valid(candidate, format != 0) && (!found || get64(candidate + 8) > generation)) {
                for (unsigned i = 0; i < sectors * 512; ++i)
                    image[i] = candidate[i];
                generation = get64(candidate + 8);
                active_bank = bank;
                found = true;
            }
        }
        if (found) {
            if (format)
                active_bank = 1; /* First v2 commit uses bank zero, preserving both v1 banks. */
            if (!fs_import(image + 512, format ? 8384 : FS_SNAPSHOT_SIZE))
                return -2;
            if (format)
                say("STORAGE V1 IMPORTED - NEXT COMMIT USES V2\n");
            return 1;
        }
    }
    if (!all_zero)
        return -2;
    generation = 0;
    active_bank = 1;
    return 0;
}
bool storage_save(void) {
    for (unsigned i = 0; i < sizeof(image); ++i)
        image[i] = 0;
    if (!fs_export(image + 512, FS_SNAPSHOT_SIZE) || generation == UINT64_MAX)
        return false;
    unsigned bank = 1 - active_bank;
    if (!sector(FIRST_SECTOR + bank * NSECT, image, true) || !flush())
        return false;
    for (unsigned i = 1; i < NSECT; ++i)
        if (!sector(FIRST_SECTOR + bank * NSECT + i, image + i * 512, true))
            return false;
    if (!flush())
        return false;
    put64(image, 0x3253464f5255454eull);
    put64(image + 8, generation + 1);
    put64(image + 16, FS_SNAPSHOT_SIZE);
    put64(image + 24, crc(image + 512, FS_SNAPSHOT_SIZE));
    put64(image + 32, crc(image, 32));
    if (!sector(FIRST_SECTOR + bank * NSECT, image, true) || !flush())
        return false;
    for (unsigned i = 0; i < 512; ++i)
        candidate[i] = 0;
    put64(candidate, 0x3254494d4d4f43ull);
    if (!sector(63, candidate, true) || !flush())
        return false;
    active_bank = bank;
    ++generation;
    return true;
}

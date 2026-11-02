/* Scripted ATA device for the actual userspace driver. No host port/disk IO. */
#include "../user/storage.c"
static unsigned registers[8], mode, words, phase, delay, latency, violations, completions;
static uint64_t ticks;
static uint16_t disk[1024][256];
enum { IDLE, TRANSFER_READ, TRANSFER_WRITE, FINISH_DATA, FLUSH };
void ata_reset(unsigned behavior) {
    for (unsigned i = 0; i < 8; ++i)
        registers[i] = 0;
    mode = behavior;
    words = phase = delay = latency = violations = completions = 0;
    ticks = 0;
}
unsigned ata_violations(void) { return violations; }
unsigned ata_completed_flushes(void) { return completions; }
static unsigned status(void) {
    if (mode == 1)
        return 0x41; /* Error */
    if (mode == 2)
        return 0x80; /* Busy forever */
    if (mode == 3)
        return 0; /* Device disappeared */
    if (latency) {
        --latency;
        return 0x40;
    }
    if (phase == FINISH_DATA) {
        if (delay) {
            --delay;
            return 0x48;
        } /* DRQ has not cleared yet */
        phase = IDLE;
    }
    if (phase == FLUSH) {
        if (delay) {
            --delay;
            return 0x80;
        }
        phase = IDLE;
        ++completions;
    }
    if (phase == TRANSFER_READ || phase == TRANSFER_WRITE) {
        if (delay) {
            --delay;
            return 0x80;
        }
        return 0x48;
    }
    return 0x40;
}
long call3(uint64_t call, uint64_t a, uint64_t b, uint64_t c) {
    if (call == 0) {
        ++ticks;
        return 0;
    }
    if (call == 5)
        return (long)ticks++;
    if (call == 6)
        return (long)b; /* Capture-free diagnostic sink. */
    if (call != 9 || a != 3)
        return -1;
    unsigned port = (unsigned)b & 65535, op = (unsigned)(b >> 16);
    if (op == 0 && (port == 0x1f7 || port == 0x3f6))
        return status();
    if (op == 2 && port >= 0x1f2 && port <= 0x1f6) {
        registers[port - 0x1f0] = (unsigned)c;
        return 0;
    }
    if (op == 2 && port == 0x1f7) {
        if (phase != IDLE) {
            ++violations;
            return -1;
        }
        words = 0;
        if (c == 0xe7) {
            phase = FLUSH;
            delay = 12;
            latency = 3;
            return 0;
        }
        if (c == 0x20 || c == 0x30) {
            phase = c == 0x20 ? TRANSFER_READ : TRANSFER_WRITE;
            delay = 8;
            return 0;
        }
        return -1;
    }
    unsigned lba = registers[3] | registers[4] << 8 | registers[5] << 16;
    if (lba >= 1024)
        return -1;
    if (port == 0x1f0 && (op == 1 || op == 3)) {
        if (delay || words >= 256 || (op == 1 && phase != TRANSFER_READ) ||
            (op == 3 && phase != TRANSFER_WRITE)) {
            ++violations;
            return -1;
        }
        long value = 0;
        if (op == 1)
            value = disk[lba][words];
        else
            disk[lba][words] = (uint16_t)c;
        if (++words == 256) {
            phase = FINISH_DATA;
            delay = 12;
        }
        return value;
    }
    return -1;
}
bool ata_test_read(unsigned lba, void *data) { return sector(lba, data, false); }
bool ata_test_write(unsigned lba, void *data) { return sector(lba, data, true); }
bool ata_test_flush(void) { return flush(); }
bool ata_test_ready(bool data) { return ready(data); }

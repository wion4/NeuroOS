#include "../user/fs.h"
#include "io.h"
#include "process.h"
#include "shell.h"
static char document[16384]; /* The initial line editor deliberately stays small. */
static bool same(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
void editor_run(const char *name) {
    unsigned n = 0;
    while (name[n])
        ++n;
    if (!n || n > 15) {
        console_write("ERROR: filename required, max 15 characters\n");
        return;
    }
    struct fs_message q = {.op = FS_STAT}, r;
    unsigned bytes = 0;
    for (unsigned i = 0; i < n; ++i)
        q.name[i] = name[i];
    if (!process_file_request(&q, &r)) {
        console_write("ERROR: file service unavailable\n");
        return;
    }
    if (r.status && r.status != -2) {
        console_write("ERROR: cannot open file\n");
        return;
    }
    if (!r.status) {
        if (r.size > sizeof(document)) {
            console_write("ERROR: file too large\n");
            return;
        }
        bytes = r.size;
        q.op = FS_READ;
        while (q.offset < bytes) {
            q.size = bytes - q.offset;
            if (q.size > 32)
                q.size = 32;
            if (!process_file_request(&q, &r) || r.status || r.size != q.size)
                return;
            for (unsigned i = 0; i < r.size; ++i)
                document[q.offset + i] = r.data[i];
            q.offset += r.size;
        }
    }
    console_write("LINE EDITOR - APPEND MODE\n.save SAVE  .cancel DISCARD  .clear REPLACE\n");
    for (unsigned i = 0; i < bytes; ++i)
        if (document[i] == '\n' || (document[i] >= 32 && document[i] <= 126))
            console_char(document[i]);
    console_write("\n: ");
    char line[96];
    unsigned length = 0;
    bool overflow = false, was_cr = false;
    for (;;) {
        char c = keyboard_poll();
        if (!c && (io_in(0x3fd) & 1))
            c = (char)io_in(0x3f8);
        if (!c) {
            __asm__ volatile("sti; hlt" ::: "memory");
            continue;
        }
        if (c == '\n' && was_cr) {
            was_cr = false;
            continue;
        }
        was_cr = c == '\r';
        if (c == '\r' || c == '\n') {
            console_write("\n");
            line[length] = 0;
            if (overflow)
                console_write("ERROR: line too long - discarded\n");
            else if (same(line, ".cancel")) {
                console_write("EDITOR CANCELLED\n");
                return;
            } else if (same(line, ".clear")) {
                bytes = 0;
                console_write("BUFFER CLEARED - DISK UNCHANGED\n");
            } else if (same(line, ".save")) {
                if (filesystem_store(name, document, bytes)) {
                    console_write("SAVED ");
                    console_number(bytes);
                    console_write(" BYTES\n");
                } else
                    console_write("ERROR: file not saved\n");
                return;
            } else if (length + 1 > sizeof(document) - bytes)
                console_write("ERROR: document full - line discarded\n");
            else {
                for (unsigned i = 0; i < length; ++i)
                    document[bytes++] = line[i];
                document[bytes++] = '\n';
            }
            length = 0;
            overflow = false;
            console_write(": ");
        } else if (c == '\b' || c == 127) {
            if (length && !overflow) {
                --length;
                console_char('\b');
            }
        } else if (c == '\t') {
            for (unsigned i = 0; i < 4; ++i)
                if (length + 1 < sizeof(line) && !overflow) {
                    line[length++] = ' ';
                    console_char(' ');
                } else
                    overflow = true;
        } else if (c >= 32 && c <= 126 && !overflow) {
            if (length + 1 < sizeof(line)) {
                line[length++] = c;
                console_char(c);
            } else
                overflow = true;
        }
    }
}

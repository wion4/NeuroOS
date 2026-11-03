/* A native, user-space desktop. Tools share this process in the first preview. */
#include "desktop.h"
#include "../kernel/version.h"
#include "abi.h"
#include "file.h"
#include "font.h"
#include "heap.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include <stdbool.h>
#define W 800
#define H 600
#define BG 0x101c29u
#define PANEL 0x172535u
#define INK 0xe5edf4u
#define MUTED 0x93a6b8u
#define EDGE 0x2c4052u
enum { HOME, FILES, NOTES, CALC, APPEARANCE, APP_COUNT };
static const char *titles[] = {"Welcome", "Files", "Notes", "Calculator", "Appearance"};
static const char *labels[] = {"Home", "Files", "Notes", "Calc", "Style"};
struct window {
    int x, y, w, h;
    bool open;
};
static struct window windows[APP_COUNT] = {{132, 78, 620, 420, true},
                                           {130, 78, 620, 430, false},
                                           {150, 90, 600, 420, false},
                                           {340, 85, 365, 425, false},
                                           {220, 110, 490, 365, false}};
static int order[APP_COUNT] = {0, 1, 2, 3, 4}, active;
static uint32_t *pixels, accent = 0x51e1ae;
static const uint32_t accents[] = {0x51e1ae, 0x6baeff, 0xc09aff, 0xffbf69};
static unsigned theme;
static bool settings_conflict;
static struct desktop_input input;
static int drag = -1, drag_x, drag_y;
static bool redraw = true, exiting;
static char notice[80] = "Welcome to your first NeuroOS desktop.";
struct file_item {
    char name[16];
    unsigned bytes;
};
static struct file_item files[FS_FILE_COUNT];
static unsigned file_count, file_page;
static int selected = -1;
static char note[8193], filename[16] = "note.txt", loaded_name[16];
static unsigned note_len, cursor, note_scroll, name_cursor = 8;
static bool dirty, name_focus;
static char expression[32] = "0", result[40] = "0";
static unsigned expr_len = 1;
enum { NO_DIALOG, OPEN_FILE, NEW_NOTE, EXIT_DESKTOP, REPLACE_FILE };
static int dialog;
static char pending_name[16];
static void message(const char *s) {
    snprintf(notice, sizeof(notice), "%s", s);
    redraw = true;
}
static bool inside(int x, int y, int w, int h) {
    return input.x >= x && input.x < x + w && input.y >= y && input.y < y + h;
}
static void rect(int x, int y, int w, int h, uint32_t c) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > W)
        w = W - x;
    if (y + h > H)
        h = H - y;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            pixels[(y + j) * W + x + i] = c;
}
static void line(int x, int y, int w, uint32_t c) { rect(x, y, w, 1, c); }
static void text(int x, int y, const char *s, int scale, uint32_t c) {
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        int index = ch >= '0' && ch <= '9'   ? ch - '0'
                    : ch >= 'A' && ch <= 'Z' ? ch - 'A' + 10
                    : ch >= 'a' && ch <= 'z' ? ch - 'a' + 36
                                             : -1;
        for (unsigned r = 0; r < 7; ++r) {
            unsigned bits = index >= 0                        ? glyphs[index][r]
                            : ch == '.' && r == 6             ? 4
                            : ch == ':' && (r == 2 || r == 5) ? 4
                            : ch == '-' && r == 3             ? 14
                            : ch == '=' && (r == 2 || r == 4) ? 14
                            : ch == '_' && r == 6             ? 31
                            : ch == '/'                       ? (r < 5 ? 1u << r : 0)
                            : ch == '>'                       ? (r == 1 || r == 5   ? 8
                                                                 : r == 2 || r == 4 ? 4
                                                                 : r == 3           ? 2
                                                                                    : 0)
                                                              : punctuation((char)ch, r);
            for (unsigned col = 0; col < 5; ++col)
                if (bits & (16u >> col))
                    rect(x + (int)col * scale, y + (int)r * scale, scale, scale, c);
        }
        x += 6 * scale;
    }
}
static void button(int x, int y, int w, const char *s, bool primary) {
    rect(x, y, w, 28, primary ? accent : EDGE);
    if (inside(x, y, w, 28)) {
        line(x, y, w, INK);
        line(x, y + 27, w, INK);
    }
    text(x + 10, y + 10, s, 1, primary ? BG : INK);
}
static void icon(int id, int x, int y, uint32_t color) {
    if (id == FILES) {
        rect(x, y + 5, 12, 6, color);
        rect(x, y + 9, 28, 19, color);
        rect(x + 3, y + 13, 22, 12, PANEL);
    } else if (id == NOTES) {
        rect(x + 4, y + 2, 20, 27, color);
        rect(x + 7, y + 5, 14, 21, PANEL);
        for (int i = 0; i < 3; ++i)
            rect(x + 10, y + 10 + i * 5, 8, 1, color);
    } else if (id == CALC) {
        rect(x + 4, y + 2, 21, 27, color);
        rect(x + 7, y + 5, 15, 6, PANEL);
        for (int i = 0; i < 6; ++i)
            rect(x + 8 + (i % 3) * 5, y + 15 + (i / 3) * 6, 3, 3, PANEL);
    } else if (id == APPEARANCE) {
        for (int i = 0; i < 4; ++i)
            rect(x + 2 + (i % 2) * 14, y + 2 + (i / 2) * 14, 11, 11, accents[i]);
    } else {
        rect(x + 3, y + 3, 5, 25, color);
        rect(x + 22, y + 3, 5, 25, color);
        for (int i = 0; i < 4; ++i)
            rect(x + 8 + i * 4, y + 4 + i * 5, 5, 5, color);
    }
}
static void front(int id) {
    for (int i = 0; i < APP_COUNT; ++i)
        if (order[i] == id) {
            for (int j = i; j < APP_COUNT - 1; ++j)
                order[j] = order[j + 1];
            order[APP_COUNT - 1] = id;
            break;
        }
    active = id;
    windows[id].open = true;
    redraw = true;
}
static void refresh_files(void) {
    file_count = 0;
    selected = -1;
    struct fs_message q = {.op = FS_LIST}, r;
    for (unsigned i = 0; i < FS_FILE_COUNT; ++i) {
        long e = nos_fs_request(&q, &r);
        if (e == -2)
            break;
        if (e) {
            message("Could not read the disk. Return to console to recover the service.");
            break;
        }
        memcpy(files[file_count].name, r.name, 16);
        files[file_count++].bytes = r.size;
        q.offset = r.offset;
    }
    if (file_page >= file_count)
        file_page = 0;
    redraw = true;
}
static void open_note(const char *name) {
    long bytes = nos_file_size(name);
    if (bytes < 0) {
        message("Could not open this file.");
        return;
    }
    if (bytes > 8192) {
        message("This editor opens text files up to 8 KiB. File left unchanged.");
        return;
    }
    /* Read into a temporary buffer so a failed read cannot destroy unsaved text. */
    char *copy = malloc((size_t)bytes + 1);
    if (!copy) {
        message("Not enough memory to open this file.");
        return;
    }
    long n = nos_file_read(name, 0, copy, (unsigned)bytes);
    bool valid = n == bytes;
    for (long i = 0; valid && i < n; ++i)
        if ((unsigned char)copy[i] > 126 ||
            ((unsigned char)copy[i] < 32 && copy[i] != '\n' && copy[i] != '\t'))
            valid = false;
    if (valid) {
        memcpy(note, copy, (size_t)n);
        note[n] = 0;
        note_len = (unsigned)n;
        cursor = note_len;
        snprintf(filename, sizeof(filename), "%s", name);
        snprintf(loaded_name, sizeof(loaded_name), "%s", name);
        name_cursor = (unsigned)strlen(filename);
        note_scroll = 0;
        dirty = false;
        name_focus = false;
        front(NOTES);
        message("Text file opened. Ctrl+S saves to disk.");
        say("DESKTOP NOTE OPENED\n");
    } else
        message("This is not a supported plain-text file. File left unchanged.");
    free(copy);
}
static void new_note(void) {
    for (unsigned i = 1; i <= 999; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "note%03u.txt", i);
        if (nos_file_size(name) == -2) {
            memcpy(filename, name, strlen(name) + 1);
            loaded_name[0] = 0;
            note_len = cursor = note_scroll = 0;
            note[0] = 0;
            dirty = false;
            name_focus = false;
            name_cursor = (unsigned)strlen(filename);
            front(NOTES);
            message("New note. Type your text, then click Save.");
            return;
        }
    }
    message("Could not choose a new filename.");
}
static bool valid_filename(void) {
    if (!filename[0])
        return false;
    for (unsigned i = 0; filename[i]; ++i) {
        char c = filename[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-'))
            return false;
    }
    return strcmp(filename, ".") && strcmp(filename, "..");
}
static void save_note(bool overwrite) {
    if (!valid_filename()) {
        message("Use 1-15 letters, digits, dots, dashes or underscores for the name.");
        return;
    }
    if (!overwrite && strcmp(filename, loaded_name)) {
        long size = nos_file_size(filename);
        if (size >= 0) {
            dialog = REPLACE_FILE;
            redraw = true;
            return;
        }
        if (size != -2) {
            message("Could not check the destination. Nothing saved.");
            return;
        }
    }
    long n = nos_file_write(filename, note, note_len);
    if (n != (long)note_len) {
        message("Save failed. Your text is still in the editor.");
        return;
    }
    memcpy(loaded_name, filename, strlen(filename) + 1);
    dirty = false;
    refresh_files();
    message("Saved to disk. Your note will survive a restart.");
    say("DESKTOP NOTE SAVED\n");
}
static void request_open(const char *name) {
    if (dirty) {
        snprintf(pending_name, sizeof(pending_name), "%s", name);
        dialog = OPEN_FILE;
        redraw = true;
    } else
        open_note(name);
}
static void request_new(void) {
    if (dirty)
        dialog = NEW_NOTE;
    else
        new_note();
    redraw = true;
}
static void request_exit(void) {
    if (dirty)
        dialog = EXIT_DESKTOP;
    else
        exiting = true;
    redraw = true;
}
static void confirm(void) {
    int action = dialog;
    dialog = NO_DIALOG;
    if (action == OPEN_FILE)
        open_note(pending_name);
    if (action == NEW_NOTE)
        new_note();
    if (action == EXIT_DESKTOP)
        exiting = true;
    if (action == REPLACE_FILE)
        save_note(true);
}
static void calc_key(char key) {
    if (key == 'C' || key == 'c') {
        strcpy(expression, "0");
        expr_len = 1;
        strcpy(result, "0");
    } else if (key == '\b') {
        if (expr_len > 1)
            expression[--expr_len] = 0;
        else
            strcpy(expression, "0");
    } else if (key == '=' || key == '\n') {
        const char *p = expression;
        long long a = 0, b = 0, value = 0;
        bool negative = false, error = false;
        if (*p == '-') {
            negative = true;
            ++p;
        }
        if (*p < '0' || *p > '9')
            error = true;
        while (*p >= '0' && *p <= '9') {
            if (a > 100000000000000000ll)
                error = true;
            if (!error)
                a = a * 10 + (*p - '0');
            ++p;
        }
        if (negative)
            a = -a;
        char op = *p;
        if (*p)
            ++p;
        else
            error = true;
        negative = false;
        if (*p == '-') {
            negative = true;
            ++p;
        }
        if (*p < '0' || *p > '9')
            error = true;
        while (*p >= '0' && *p <= '9') {
            if (b > 100000000000000000ll)
                error = true;
            if (!error)
                b = b * 10 + (*p - '0');
            ++p;
        }
        if (negative)
            b = -b;
        if (*p)
            error = true;
        if (!error) {
            if (op == '+')
                error = __builtin_add_overflow(a, b, &value);
            else if (op == '-')
                error = __builtin_sub_overflow(a, b, &value);
            else if (op == '*')
                error = __builtin_mul_overflow(a, b, &value);
            else if (op == '/') {
                if (!b)
                    error = true;
                else
                    value = a / b;
            } else
                error = true;
        }
        if (error)
            strcpy(result, "Invalid / too large");
        else {
            snprintf(result, sizeof(result), "%lld", value);
            say("DESKTOP CALC RESULT\n");
        }
    } else if ((key >= '0' && key <= '9') || key == '+' || key == '-' || key == '*' || key == '/') {
        if (expr_len == 1 && expression[0] == '0')
            expr_len = 0;
        if (expr_len < 20) {
            expression[expr_len++] = key;
            expression[expr_len] = 0;
        }
    }
    redraw = true;
}
static void cursor_position(unsigned pos, unsigned *row, unsigned *column) {
    unsigned r = 0, c = 0;
    for (unsigned i = 0; i < pos; ++i) {
        if (note[i] == '\n') {
            ++r;
            c = 0;
        } else {
            ++c;
            if (c == 46) {
                ++r;
                c = 0;
            }
        }
    }
    *row = r;
    *column = c;
}
static void vertical_cursor(bool down) {
    unsigned row, col;
    cursor_position(cursor, &row, &col);
    if (!down && !row) {
        cursor = 0;
        return;
    }
    unsigned target = down ? row + 1 : row - 1;
    for (unsigned i = 0; i <= note_len; ++i) {
        unsigned r, c;
        cursor_position(i, &r, &c);
        if (r > target || (r == target && (c >= col || note[i] == '\n' || i == note_len))) {
            cursor = i;
            return;
        }
    }
    cursor = note_len;
}
static void note_key(unsigned key) {
    if (key == 19) {
        save_note(false);
        return;
    }
    if (key == '\t') {
        name_focus = !name_focus;
        redraw = true;
        return;
    }
    if (name_focus) {
        unsigned n = (unsigned)strlen(filename);
        if (key == '\b' && name_cursor) {
            memmove(filename + name_cursor - 1, filename + name_cursor, n - name_cursor + 1);
            --name_cursor;
            dirty = true;
        } else if (key == 0x83 && name_cursor)
            --name_cursor;
        else if (key == 0x84 && name_cursor < n)
            ++name_cursor;
        else if (key == '\n')
            name_focus = false;
        else if (key >= 32 && key <= 126 && n < 15) {
            memmove(filename + name_cursor + 1, filename + name_cursor, n - name_cursor + 1);
            filename[name_cursor++] = (char)key;
            dirty = true;
        }
    } else {
        if (key == '\b' && cursor) {
            memmove(note + cursor - 1, note + cursor, note_len - cursor + 1);
            --cursor;
            --note_len;
            dirty = true;
        } else if (key == 0x83 && cursor)
            --cursor;
        else if (key == 0x84 && cursor < note_len)
            ++cursor;
        else if (key == 0x81)
            vertical_cursor(false);
        else if (key == 0x82)
            vertical_cursor(true);
        else if ((key >= 32 && key <= 126) || key == '\n') {
            if (note_len < 8192) {
                memmove(note + cursor + 1, note + cursor, note_len - cursor + 1);
                note[cursor++] = (char)key;
                ++note_len;
                dirty = true;
            } else
                message("Note is full (8 KiB). Save this file and start another note.");
        }
    }
    redraw = true;
}
static void draw_home(struct window *w) {
    int x = w->x + 26, y = w->y + 68;
    text(x, y, "A small beginning.", 3, INK);
    text(x, y + 32, "A real operating system.", 2, accent);
    text(x, y + 75, "Your files, your notes, your desktop.", 1, MUTED);
    text(x, y + 94, "Running directly on the NeuroOS kernel.", 1, MUTED);
    for (int i = 1; i <= 3; ++i) {
        int cx = x + (i - 1) * 185;
        rect(cx, y + 135, 172, 112, 0x203244);
        icon(i, cx + 16, y + 149, accent);
        text(cx + 16, y + 190, titles[i], 1, INK);
        text(cx + 16, y + 210,
             i == 1   ? "Browse your disk"
             : i == 2 ? "Write and save"
                      : "Do a quick sum",
             1, MUTED);
    }
    text(x, y + 280, "PREVIEW " NEUROOS_VERSION "  /  Native x86-64  /  No AI yet", 1, MUTED);
    text(x, y + 304, "Drag a title bar. Use the dock to switch tools.", 1, MUTED);
}
static void draw_files(struct window *w) {
    int x = w->x + 20, y = w->y + 50;
    button(x, y, 100, "New note", true);
    button(x + 112, y, 90, "Refresh", false);
    button(x + 214, y, 110, "Open text", false);
    text(x, y + 48, "NAME", 1, MUTED);
    text(x + 410, y + 48, "BYTES", 1, MUTED);
    line(x, y + 64, w->w - 40, EDGE);
    for (unsigned row = 0; row < 9; ++row) {
        unsigned i = file_page + row;
        if (i >= file_count)
            break;
        int yy = y + 73 + (int)row * 27;
        if ((int)i == selected)
            rect(x, yy - 5, w->w - 40, 26, EDGE);
        rect(x + 4, yy, 9, 12, accent);
        text(x + 26, yy + 3, files[i].name, 1, INK);
        char size[20];
        snprintf(size, sizeof(size), "%u", files[i].bytes);
        text(x + 410, yy + 3, size, 1, MUTED);
    }
    char status[55];
    snprintf(status, sizeof(status), "%u files  /  Text editor up to 8 KiB", file_count);
    text(x, y + 330, status, 1, MUTED);
    button(x + 400, y + 316, 76, "Prev", false);
    button(x + 486, y + 316, 76, "Next", false);
}
static void draw_notes(struct window *w) {
    int x = w->x + 20, y = w->y + 48;
    text(x, y + 11, "FILE", 1, MUTED);
    rect(x + 42, y, 230, 28, name_focus ? EDGE : BG);
    text(x + 52, y + 10, filename, 1, INK);
    if (name_focus)
        rect(x + 52 + (int)name_cursor * 6, y + 7, 1, 14, accent);
    button(x + 286, y, 80, "Save", true);
    button(x + 378, y, 74, "New", false);
    text(x + 466, y + 10, dirty ? "Unsaved" : "Saved", 1, dirty ? 0xffbf69 : MUTED);
    int bx = x, by = y + 44;
    rect(bx, by, w->w - 40, 264, 0x0c1722);
    unsigned cr, cc;
    cursor_position(cursor, &cr, &cc);
    if (cr < note_scroll)
        note_scroll = cr;
    if (cr >= note_scroll + 15)
        note_scroll = cr - 14;
    unsigned row = 0, col = 0;
    for (unsigned i = 0; i < note_len; ++i) {
        char c = note[i];
        if (c == '\n') {
            ++row;
            col = 0;
            continue;
        }
        if (row >= note_scroll && row < note_scroll + 15) {
            char s[2] = {c == '\t' ? ' ' : c, 0};
            text(bx + 8 + (int)col * 12, by + 9 + (int)(row - note_scroll) * 17, s, 2, INK);
        }
        if (++col == 46) {
            ++row;
            col = 0;
        }
    }
    if (!name_focus)
        rect(bx + 8 + (int)cc * 12, by + 9 + (int)(cr - note_scroll) * 17, 2, 14, accent);
    char status[70];
    snprintf(status, sizeof(status), "%u / 8192 bytes    Ctrl+S save    Tab: name / text",
             note_len);
    text(x, y + 328, status, 1, MUTED);
}
static const char *calc_keys[] = {"7", "8", "9", "/", "4", "5", "6", "*",
                                  "1", "2", "3", "-", "C", "0", "=", "+"};
static void draw_calc(struct window *w) {
    int x = w->x + 22, y = w->y + 52;
    rect(x, y, w->w - 44, 83, BG);
    text(x + 12, y + 13, expression, 1, MUTED);
    text(x + 12, y + 43, result, 2, INK);
    for (int i = 0; i < 16; ++i) {
        int bx = x + (i % 4) * 80, by = y + 100 + (i / 4) * 49;
        rect(bx, by, 68, 38, i == 14 ? accent : EDGE);
        if (inside(bx, by, 68, 38))
            line(bx, by, 68, INK);
        text(bx + 25, by + 12, calc_keys[i], 2, i == 14 ? BG : INK);
    }
    text(x, y + 317, "Integers. One operation at a time.", 1, MUTED);
}
static void draw_appearance(struct window *w) {
    int x = w->x + 26, y = w->y + 65;
    text(x, y, "Make it yours.", 2, INK);
    text(x, y + 40, "Choose an accent colour", 1, MUTED);
    for (int i = 0; i < 4; ++i) {
        rect(x + i * 100, y + 72, 78, 66, accents[i]);
        if (theme == (unsigned)i)
            text(x + i * 100 + 24, y + 98, "OK", 2, BG);
    }
    text(x, y + 168, "The choice is saved on your disk.", 1, MUTED);
    text(x, y + 191, "Desktop preview / US keyboard / ASCII text", 1, MUTED);
    text(x, y + 216, "Tools currently share one desktop process.", 1, MUTED);
    text(x, y + 239, "Files run in a separate isolated service.", 1, MUTED);
}
static void paint(void) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            unsigned r = 10 + (unsigned)y / 100, g = 20 + (unsigned)y / 80,
                     b = 32 + (unsigned)y / 40;
            int dx = x - 660, dy = y - 180;
            int radius = dx * dx + dy * dy;
            if ((radius > 190 * 190 && radius < 192 * 192) ||
                (radius > 260 * 260 && radius < 262 * 262)) {
                r += 8;
                g += 17;
                b += 19;
            }
            pixels[y * W + x] = (r << 16) | (g << 8) | b;
        }
    rect(0, 0, W, 34, 0x111e2b);
    icon(HOME, 16, 2, accent);
    text(57, 12, "NeuroOS", 1, INK);
    text(624, 12, "DESKTOP " NEUROOS_VERSION, 1, MUTED);
    for (int id = 1; id <= 3; ++id) {
        icon(id, 37, 65 + (id - 1) * 88, accent);
        text(26, 106 + (id - 1) * 88, labels[id], 1, INK);
    }
    text(24, 431, "YOUR", 1, MUTED);
    text(24, 449, "SPACE.", 1, MUTED);
    for (int i = 0; i < APP_COUNT; ++i) {
        int id = order[i];
        struct window *w = &windows[id];
        if (!w->open)
            continue;
        rect(w->x + 7, w->y + 8, w->w, w->h, 0x070f18);
        rect(w->x, w->y, w->w, w->h, PANEL);
        rect(w->x, w->y, w->w, 35, id == active ? EDGE : 0x203040);
        if (id == active)
            rect(w->x, w->y, 3, 35, accent);
        text(w->x + 15, w->y + 14, titles[id], 1, INK);
        button(w->x + w->w - 38, w->y + 4, 30, "x", false);
        if (id == HOME)
            draw_home(w);
        if (id == FILES)
            draw_files(w);
        if (id == NOTES)
            draw_notes(w);
        if (id == CALC)
            draw_calc(w);
        if (id == APPEARANCE)
            draw_appearance(w);
    }
    rect(0, 533, W, 67, 0x111e2b);
    line(0, 533, W, EDGE);
    for (int id = 0; id < APP_COUNT; ++id) {
        int x = 18 + id * 108;
        if (windows[id].open)
            rect(x, 542, 96, 47, id == active ? EDGE : PANEL);
        icon(id, x + 8, 547, accent);
        text(x + 43, 558, labels[id], 1, INK);
    }
    button(685, 550, 96, "Console", false);
    text(16, 516, notice, 1, MUTED);
    if (dialog) {
        rect(181, 203, 452, 151, 0x060d15);
        rect(174, 196, 452, 151, EDGE);
        text(196, 216,
             dialog == REPLACE_FILE ? "Replace the existing file?" : "You have unsaved changes.", 2,
             INK);
        text(196, 250,
             dialog == REPLACE_FILE ? "The file on disk will be replaced."
                                    : "Save in Notes first, or discard these changes.",
             1, MUTED);
        button(280, 292, 120, "Cancel", false);
        button(418, 292, 175, dialog == REPLACE_FILE ? "Replace" : "Discard and continue", true);
    }
    /* Software pointer is part of the same backbuffer, so no trails. */
    for (int y = 0; y < 17; ++y)
        for (int x = 0; x <= y / 2; ++x)
            rect(input.x + x, input.y + y, 1, 1, x == 0 || x == y / 2 || y == 16 ? 0x07101a : INK);
    (void)call3(11, 1, (uintptr_t)pixels, W * H * 4);
    redraw = false;
}
static void click(void) {
    if (dialog) {
        if (inside(280, 292, 120, 28))
            dialog = NO_DIALOG;
        else if (inside(418, 292, 175, 28))
            confirm();
        redraw = true;
        return;
    }
    if (input.y >= 533) {
        for (int i = 0; i < APP_COUNT; ++i)
            if (inside(18 + i * 108, 542, 96, 47)) {
                if (i == FILES)
                    refresh_files();
                front(i);
                return;
            }
        if (inside(685, 550, 96, 28))
            request_exit();
        return;
    }
    for (int i = APP_COUNT - 1; i >= 0; --i) {
        int id = order[i];
        struct window *w = &windows[id];
        if (!w->open || !inside(w->x, w->y, w->w, w->h))
            continue;
        front(id);
        if (inside(w->x + w->w - 38, w->y + 4, 30, 28)) {
            w->open = false;
            return;
        }
        if (input.y < w->y + 35) {
            drag = id;
            drag_x = input.x - w->x;
            drag_y = input.y - w->y;
            return;
        }
        int x = w->x + 20, y = w->y + 50;
        if (id == HOME) {
            for (int j = 1; j <= 3; ++j)
                if (inside(w->x + 26 + (j - 1) * 185, w->y + 203, 172, 112)) {
                    if (j == FILES)
                        refresh_files();
                    front(j);
                    return;
                }
        } else if (id == FILES) {
            if (inside(x, y, 100, 28))
                request_new();
            else if (inside(x + 112, y, 90, 28))
                refresh_files();
            else if (inside(x + 214, y, 110, 28) && selected >= 0)
                request_open(files[selected].name);
            else if (inside(x + 400, y + 316, 76, 28)) {
                file_page = file_page >= 9 ? file_page - 9 : 0;
                selected = -1;
            } else if (inside(x + 486, y + 316, 76, 28)) {
                if (file_page + 9 < file_count)
                    file_page += 9;
                selected = -1;
            } else
                for (unsigned row = 0; row < 9; ++row)
                    if (inside(x, y + 68 + (int)row * 27, w->w - 40, 26) &&
                        file_page + row < file_count)
                        selected = (int)(file_page + row);
        } else if (id == NOTES) {
            y = w->y + 48;
            if (inside(x + 42, y, 230, 28)) {
                name_focus = true;
                name_cursor = (unsigned)strlen(filename);
            } else if (inside(x + 286, y, 80, 28))
                save_note(false);
            else if (inside(x + 378, y, 74, 28))
                request_new();
            else if (inside(x, y + 44, w->w - 40, 264)) {
                name_focus = false;
                unsigned targetrow =
                    note_scroll + (unsigned)(input.y < y + 53 ? 0 : input.y - (y + 53)) / 17;
                unsigned targetcol = (unsigned)(input.x - x < 8 ? 0 : input.x - x - 8) / 12;
                cursor = note_len;
                for (unsigned j = 0; j <= note_len; ++j) {
                    unsigned r, c;
                    cursor_position(j, &r, &c);
                    if (r > targetrow || (r == targetrow && (c >= targetcol || note[j] == '\n'))) {
                        cursor = j;
                        break;
                    }
                }
            }
        } else if (id == CALC) {
            for (int j = 0; j < 16; ++j)
                if (inside(w->x + 22 + (j % 4) * 80, w->y + 152 + (j / 4) * 49, 68, 38))
                    calc_key(calc_keys[j][0]);
        } else if (id == APPEARANCE) {
            for (unsigned j = 0; j < 4; ++j)
                if (inside(w->x + 26 + (int)j * 100, w->y + 137, 78, 66)) {
                    if (settings_conflict) {
                        message(
                            "desktop.cfg is an existing user file. Move it in the console first.");
                        break;
                    }
                    char choice = (char)('0' + j);
                    if (nos_file_write("desktop.cfg", &choice, 1) == 1) {
                        theme = j;
                        accent = accents[j];
                        message("Accent saved.");
                        say("DESKTOP THEME SAVED\n");
                    } else
                        message("Could not save appearance. Your previous choice is preserved.");
                }
        }
        redraw = true;
        return;
    }
    for (int id = 1; id <= 3; ++id)
        if (inside(20, 60 + (id - 1) * 88, 90, 72)) {
            if (id == FILES)
                refresh_files();
            front(id);
            return;
        }
}
int user_main(void) {
    pixels = malloc(W * H * 4);
    if (!pixels) {
        say("DESKTOP OUT OF MEMORY\n");
        return 1;
    }
    char choice = 0;
    long config_size = nos_file_size("desktop.cfg");
    if (config_size == 1 && nos_file_read("desktop.cfg", 0, &choice, 1) == 1 && choice >= '0' &&
        choice <= '3') {
        theme = (unsigned)(choice - '0');
        accent = accents[theme];
    } else
        settings_conflict = config_size != -2;
    /* New unsaved editor buffer; never overwrite a user's existing note at boot. */
    strcpy(note, "Hello from NeuroOS!\n\nThis is a real note. Edit me, then click Save.\n");
    note_len = (unsigned)strlen(note);
    cursor = note_len;
    dirty = true;
    say("DESKTOP READY\n");
    struct desktop_input old = {.x = -1, .y = -1};
    while (!exiting) {
        if (call3(11, 0, (uintptr_t)&input, 0) < 0) {
            free(pixels);
            return 2;
        }
        if (input.x != old.x || input.y != old.y || input.buttons != old.buttons)
            redraw = true;
        if ((input.buttons & 1) && !(old.buttons & 1))
            click();
        if (!(input.buttons & 1))
            drag = -1;
        if (drag >= 0) {
            struct window *w = &windows[drag];
            w->x = input.x - drag_x;
            w->y = input.y - drag_y;
            if (w->x < 0)
                w->x = 0;
            if (w->x > W - w->w)
                w->x = W - w->w;
            if (w->y < 35)
                w->y = 35;
            if (w->y > 515 - w->h)
                w->y = 515 - w->h;
            redraw = true;
        }
        if (input.key) {
            if (dialog) {
                if (input.key == 27)
                    dialog = NO_DIALOG;
                redraw = true;
            } else if (windows[active].open && active == NOTES)
                note_key(input.key);
            else if (windows[active].open && active == CALC)
                calc_key((char)input.key);
        }
        if (redraw)
            paint();
        old = input;
        (void)call3(12, 1, 0, 0);
    }
    free(pixels);
    say("DESKTOP CLOSED\n");
    return 0;
}

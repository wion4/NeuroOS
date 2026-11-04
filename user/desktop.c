/* A native, user-space desktop. Tools share this process in the first preview. */
#include "desktop.h"
#include "../kernel/version.h"
#include "abi.h"
#include "ai.h"
#include "cyrillic.h"
#include "events.h"
#include "file.h"
#include "font.h"
#include "heap.h"
#include "keyboard.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "text.h"
#include <stdbool.h>
#define W 800
#define H 600
#define BG 0x101c29u
#define PANEL 0x172535u
#define INK 0xe5edf4u
#define MUTED 0x93a6b8u
#define EDGE 0x2c4052u
enum { HOME, FILES, NOTES, CALC, APPEARANCE, PROGRAMS, AI, EVENTS, APP_COUNT };
static const char *titles[] = {"Welcome",    "Files",    "Notes",        "Calculator",
                               "Appearance", "Programs", "AI text demo", "Task events"};
static const char *labels[] = {"Home", "Files", "Notes", "Calc", "Style", "Apps", "AI", "Events"};
struct window {
    int x, y, w, h;
    bool open;
};
static struct window windows[APP_COUNT] = {{132, 78, 620, 420, true},   {130, 78, 620, 430, false},
                                           {150, 90, 600, 420, false},  {340, 85, 365, 425, false},
                                           {220, 110, 490, 365, false}, {140, 85, 620, 425, false},
                                           {136, 85, 620, 430, false},  {150, 95, 610, 400, false}};
static int order[APP_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7}, active;
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
static struct text_document document;
#define note document.now.data
#define note_len document.now.length
#define cursor document.now.cursor
static char filename[16] = "note.txt", loaded_name[16], saved_note[8193],
            saved_filename[16] = "note.txt";
static char clipboard[8193];
static unsigned clipboard_bytes;
static unsigned note_scroll, name_cursor = 8;
static struct keyboard_layout layout;
static bool selecting, note_maximized;
static struct window note_restore;
static bool dirty, name_focus;
static char expression[32] = "0", result[40] = "0";
static unsigned expr_len = 1;
enum { NO_DIALOG, OPEN_FILE, NEW_NOTE, EXIT_DESKTOP, REPLACE_FILE };
static int dialog;
static char pending_name[16];
static char program_name[16] = "hello.nos", program_arguments[80];
static bool program_files, program_arguments_focus = true, program_has_result;
static unsigned program_scroll;
static struct desktop_launch_result program_result;
static uint64_t program_job;
static struct native_job_info program_status;
static struct nos_event event_cache[NOS_EVENT_RETAINED];
static struct nos_event_query event_query;
static uint64_t event_cursor, event_lost;
static unsigned event_count, event_head, event_offset;
static bool event_error;
static char ai_prompt[56] = "Once upon a time";
static struct ai_session ai_result;
static bool ai_has_result, ai_busy;
static unsigned ai_scroll;
static void paint(void);
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
    unsigned remaining = (unsigned)strlen(s);
    while (remaining) {
        uint32_t ch;
        unsigned consumed = utf8_decode(s, remaining, &ch);
        if (!consumed) {
            consumed = 1;
            ch = '?';
        }
        s += consumed;
        remaining -= consumed;
        int index = ch >= '0' && ch <= '9'   ? ch - '0'
                    : ch >= 'A' && ch <= 'Z' ? ch - 'A' + 10
                    : ch >= 'a' && ch <= 'z' ? ch - 'a' + 36
                                             : -1;
        for (unsigned r = 0; r < 7; ++r) {
            unsigned bits = ch >= 0x410 && ch <= 0x42f        ? cyrillic_upper[ch - 0x410][r]
                            : ch >= 0x430 && ch <= 0x44f      ? cyrillic_lower[ch - 0x430][r]
                            : ch == 0x401                     ? (r == 0   ? 10
                                                                 : r == 1 ? 0
                                                                          : glyphs['E' - 'A' + 10][r])
                            : ch == 0x451                     ? (r == 0   ? 10
                                                                 : r == 1 ? 0
                                                                          : glyphs['e' - 'a' + 36][r])
                            : ch > 127                        ? (r == 0 || r == 6 ? 31 : 17)
                            : index >= 0                      ? glyphs[index][r]
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
    } else if (id == PROGRAMS) {
        rect(x + 2, y + 4, 26, 23, color);
        rect(x + 4, y + 7, 22, 17, PANEL);
        text(x + 7, y + 10, ">_", 1, color);
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
    valid = valid && text_valid(copy, (unsigned)bytes);
    if (valid) {
        (void)text_load(&document, copy, (unsigned)n);
        memcpy(saved_note, note, note_len + 1);
        snprintf(filename, sizeof(filename), "%s", name);
        snprintf(loaded_name, sizeof(loaded_name), "%s", name);
        memcpy(saved_filename, filename, strlen(filename) + 1);
        name_cursor = (unsigned)strlen(filename);
        note_scroll = 0;
        dirty = false;
        name_focus = false;
        front(NOTES);
        message("Text file opened. Ctrl+S saves to disk.");
        say("DESKTOP NOTE OPENED\n");
    } else
        message("Not a valid UTF-8 text file. File left unchanged.");
    free(copy);
}
static void new_note(void) {
    for (unsigned i = 1; i <= 999; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "note%03u.txt", i);
        if (nos_file_size(name) == -2) {
            memcpy(filename, name, strlen(name) + 1);
            loaded_name[0] = 0;
            (void)text_load(&document, "", 0);
            note_scroll = 0;
            saved_note[0] = 0;
            memcpy(saved_filename, filename, strlen(filename) + 1);
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
            message(size == -16 ? "File service busy. Your text is preserved; save later."
                                : "Could not check the destination. Nothing saved.");
            if (size == -16)
                say("DESKTOP NOTE SAVE BUSY\n");
            return;
        }
    }
    long n = nos_file_write(filename, note, note_len);
    if (n != (long)note_len) {
        message(n == -16 ? "File service busy. Your text is preserved; save later."
                         : "Save failed. Your text is still in the editor.");
        if (n == -16)
            say("DESKTOP NOTE SAVE BUSY\n");
        return;
    }
    memcpy(loaded_name, filename, strlen(filename) + 1);
    dirty = false;
    memcpy(saved_note, note, note_len + 1);
    memcpy(saved_filename, filename, strlen(filename) + 1);
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
static unsigned note_columns(void) { return (unsigned)(windows[NOTES].w - 56) / 12; }
static unsigned note_rows(void) { return (unsigned)(windows[NOTES].h - 165) / 17; }
static void cursor_position(unsigned pos, unsigned *row, unsigned *column) {
    text_position(&document, pos, note_columns(), row, column);
}
static void note_key(unsigned key) {
    if (key == 19) {
        save_note(false);
        return;
    }
    if (key == '\t' && name_focus) {
        name_focus = false;
        redraw = true;
        return;
    }
    if (key == 12) {
        name_focus = true;
        redraw = true;
        return;
    } /* Ctrl+L: filename. */
    if (name_focus) {
        unsigned n = (unsigned)strlen(filename);
        if (key == '\b' && name_cursor) {
            memmove(filename + name_cursor - 1, filename + name_cursor, n - name_cursor + 1);
            --name_cursor;
        } else if (key == KEY_LEFT && name_cursor)
            --name_cursor;
        else if (key == KEY_RIGHT && name_cursor < n)
            ++name_cursor;
        else if (key == KEY_DELETE && name_cursor < n)
            memmove(filename + name_cursor, filename + name_cursor + 1, n - name_cursor);
        else if (key == KEY_HOME)
            name_cursor = 0;
        else if (key == KEY_END)
            name_cursor = n;
        else if (key == '\n')
            name_focus = false;
        else if (key >= 32 && key <= 126 && n < 15) {
            memmove(filename + name_cursor + 1, filename + name_cursor, n - name_cursor + 1);
            filename[name_cursor++] = (char)key;
        }
    } else {
        bool success = true;
        if (key == 1) {
            document.now.anchor = 0;
            cursor = note_len;
        } else if (key == 3 || key == 24) {
            unsigned first, count = text_selection(&document, &first);
            if (count) {
                memcpy(clipboard, note + first, count);
                clipboard_bytes = count;
                clipboard[count] = 0;
                if (key == 24)
                    success = text_insert(&document, "", 0);
            }
        } else if (key == 22) {
            if (clipboard_bytes)
                success = text_insert(&document, clipboard, clipboard_bytes);
        } else if (key == 26 || key == 25)
            (void)text_undo(&document, key == 25 || (input.modifiers & INPUT_SHIFT));
        else if (key == '\b')
            success = text_erase(&document, true);
        else if (key == KEY_DELETE)
            success = text_erase(&document, false);
        else if (key >= KEY_UP && key <= KEY_PAGEDOWN)
            text_move(&document, key, (input.modifiers & INPUT_SHIFT) != 0,
                      (input.modifiers & INPUT_CONTROL) != 0, note_columns());
        else if (key == '\t')
            success = text_insert(&document, "    ", 4);
        else if ((key >= 32 && key <= 0x10ffff) || key == '\n')
            success = text_codepoint(&document, key);
        if (!success)
            message("Text limit reached (8 KiB). The current document is preserved.");
    }
    dirty = strcmp(note, saved_note) || strcmp(filename, saved_filename);
    redraw = true;
}
static bool program_suffix(const char *name) {
    size_t n = strlen(name);
    return n >= 5 && !strcmp(name + n - 4, ".nos");
}
static bool program_settings_locked(void) {
    if (!program_job)
        return false;
    message("Stop the current program before changing its launch settings or permissions.");
    say("DESKTOP JOB SETTINGS LOCKED\n");
    return true;
}
static void open_file(const char *name) {
    if (program_suffix(name)) {
        if (program_settings_locked()) {
            front(PROGRAMS);
            return;
        }
        snprintf(program_name, sizeof(program_name), "%s", name);
        program_arguments[0] = 0;
        program_files = false;
        program_arguments_focus = true;
        program_has_result = false;
        program_scroll = 0;
        front(PROGRAMS);
        message("Native program selected. File access is off by default.");
    } else
        request_open(name);
}
static void build_source(const char *name) {
    if (program_settings_locked()) {
        front(PROGRAMS);
        return;
    }
    size_t n = strlen(name);
    if (n < 3 || strcmp(name + n - 2, ".c") || n > 13) {
        message("Choose a C source ending in .c (name up to 13 characters).");
        return;
    }
    char output[16];
    memcpy(output, name, n - 2);
    memcpy(output + n - 2, ".nos", 5);
    snprintf(program_name, sizeof(program_name), "compiler.nos");
    snprintf(program_arguments, sizeof(program_arguments), "%s %s", name, output);
    program_files = false;
    program_arguments_focus = true;
    program_has_result = false;
    front(PROGRAMS);
    message(
        "Compiler needs file access: enable it, then Run. Existing files are preserved on error.");
}
static void run_program(void) {
    if (program_job) {
        long e = call3(20, NOS_JOB_CANCEL, program_job, 0);
        message(e < 0 ? "Program is already finishing. Waiting for cleanup."
                      : "Stopping program and closing its file session...");
        return;
    }
    if (ai_busy) {
        message("Wait for AI generation, or cancel it in AI demo before running a program.");
        return;
    }
    if (!program_suffix(program_name)) {
        message("Choose an executable with the .nos extension.");
        return;
    }
    struct desktop_launch_request q = {.flags = program_files ? NOS_LAUNCH_FILES : 0};
    snprintf(q.name, sizeof(q.name), "%s", program_name);
    snprintf(q.arguments, sizeof(q.arguments), "%s", program_arguments);
    program_has_result = false;
    message("Loading native program...");
    paint();
    long e = call3(20, NOS_JOB_START, (uintptr_t)&q, sizeof(q));
    if (e < 0) {
        message("Program could not start. File service unavailable or request rejected.");
        return;
    }
    program_job = (uint64_t)e;
    program_status = (struct native_job_info){.id = program_job, .state = NOS_JOB_LOADING};
    program_result = (struct desktop_launch_result){0};
    program_has_result = true;
    program_scroll = 0;
    message("Loading in background. You can keep editing; Stop cancels it.");
    say("DESKTOP PROGRAM STARTED\n");
}
static void program_poll(void) {
    if (!program_job)
        return;
    uint32_t old_state = program_status.state, old_bytes = program_result.bytes;
    program_status.id = program_job;
    if (call3(20, NOS_JOB_POLL, (uintptr_t)&program_status, sizeof(program_status)) < 0) {
        message("Could not read program status. Use Console to close this session.");
        return;
    }
    program_result = program_status.result;
    if (old_state != program_status.state || old_bytes != program_result.bytes)
        redraw = true;
    if (old_state == NOS_JOB_LOADING && program_status.state == NOS_JOB_RUNNING)
        message("Program is running. You can keep working; Stop cancels it.");
    if (program_status.state != NOS_JOB_FINISHED)
        return;
    if (call3(20, NOS_JOB_REAP, program_job, 0) < 0) {
        message("Program cleanup has not finished yet.");
        return;
    }
    program_job = 0;
    if (program_result.kind == NOS_LAUNCH_LOAD_ERROR)
        message("Could not load this file as a NeuroOS executable.");
    else if (program_result.kind == NOS_LAUNCH_FAULTED)
        message("Program fault contained. Your desktop and notes are still here.");
    else if (program_result.kind == NOS_LAUNCH_TIMED_OUT)
        message("Program reached its CPU time limit.");
    else if (program_result.kind == NOS_LAUNCH_CANCELLED)
        message("Program cancelled. Its memory and file session were released.");
    else
        message("Program finished. Your desktop and unsaved notes were preserved.");
    if (program_status.cleanup_error)
        message("Program stopped. File service unavailable; use Console to restart it.");
    else
        refresh_files();
    say("DESKTOP PROGRAM RESULT\n");
}
static void program_key(uint32_t key) {
    if (program_job) {
        if (key == '\n' || key == 27)
            run_program();
        else
            (void)program_settings_locked();
        return;
    }
    if (key == '\t') {
        program_arguments_focus = !program_arguments_focus;
        redraw = true;
        return;
    }
    if (key == '\n') {
        run_program();
        return;
    }
    char *field = program_arguments_focus ? program_arguments : program_name;
    unsigned length = (unsigned)strlen(field), limit = program_arguments_focus ? 79 : 15;
    if (!program_arguments_focus &&
        ((key == '\b' && length) || (key >= 32 && key <= 126 && length < limit)))
        program_files = false;
    if (key == '\b' && length)
        field[length - 1] = 0;
    else if (key >= 32 && key <= 126 && length < limit) {
        field[length] = (char)key;
        field[length + 1] = 0;
    }
    redraw = true;
}
static unsigned output_rows(void) {
    unsigned rows = 1, col = 0;
    for (unsigned i = 0; i < program_result.bytes; ++i) {
        if (program_result.output[i] == '\n') {
            ++rows;
            col = 0;
        } else if (++col == 92) {
            ++rows;
            col = 0;
        }
    }
    return rows;
}
static void draw_programs(struct window *w) {
    int x = w->x + 20, y = w->y + 50;
    text(x, y + 9, "FILE", 1, MUTED);
    rect(x + 38, y, 185, 28, program_arguments_focus ? BG : EDGE);
    text(x + 47, y + 10, program_name, 1, INK);
    button(x + 237, y, 80, program_job ? "Stop" : "Run", true);
    button(x + 330, y, 245,
           program_job
               ? (program_files ? "File access: allowed (fixed)" : "File access: off (fixed)")
               : (program_files ? "[x] Allow file access" : "[ ] Allow file access"),
           false);
    text(x, y + 49, "ARGS", 1, MUTED);
    rect(x + 38, y + 39, w->w - 78, 28, program_arguments_focus ? EDGE : BG);
    unsigned args_length = (unsigned)strlen(program_arguments);
    text(x + 47, y + 49, program_arguments + (args_length > 75 ? args_length - 75 : 0), 1, INK);
    if (program_arguments_focus && !program_job)
        rect(x + 47 + (int)(args_length > 75 ? 75 : args_length) * 6, y + 46, 1, 14, accent);
    rect(x, y + 81, w->w - 40, 235, BG);
    if (program_has_result) {
        unsigned row = 0, col = 0;
        for (unsigned i = 0; i < program_result.bytes; ++i) {
            char ch = program_result.output[i];
            if (ch == '\n') {
                ++row;
                col = 0;
                continue;
            }
            if (row >= program_scroll && row < program_scroll + 18) {
                char value[2] = {ch, 0};
                text(x + 8 + (int)col * 6, y + 91 + (int)(row - program_scroll) * 12, value, 1,
                     INK);
            }
            if (++col == 92) {
                ++row;
                col = 0;
            }
        }
        char status[70];
        if (program_job)
            snprintf(status, sizeof(status),
                     program_status.state == NOS_JOB_CLOSING ? "Closing file session..."
                     : program_status.state == NOS_JOB_LOADING ? "Loading / Stop to cancel"
                                                             : "Running / Stop to cancel");
        else if (program_result.kind == NOS_LAUNCH_EXITED)
            snprintf(status, sizeof(status), "Exit code %llu%s",
                     (unsigned long long)program_result.exit_code,
                     program_result.truncated ? " / output truncated" : "");
        else if (program_result.kind == NOS_LAUNCH_FAULTED)
            snprintf(status, sizeof(status), "Contained fault %u", program_result.fault);
        else if (program_result.kind == NOS_LAUNCH_TIMED_OUT)
            snprintf(status, sizeof(status), "CPU time limit reached");
        else if (program_result.kind == NOS_LAUNCH_CANCELLED)
            snprintf(status, sizeof(status), "Cancelled");
        else
            snprintf(status, sizeof(status), "Load error");
        text(x, y + 338, status, 1, accent);
    } else
        text(x + 12, y + 101, "Choose a .nos file in Files, then Run.", 1, MUTED);
    button(x + 400, y + 326, 76, "Up", false);
    button(x + 486, y + 326, 76, "Down", false);
}
static void events_poll(void) {
    if (!windows[EVENTS].open)
        return;
    uint32_t old_flags = event_query.flags;
    event_query = (struct nos_event_query){
        .version = NOS_EVENT_VERSION, .capacity = NOS_EVENT_BATCH, .after = event_cursor};
    long e = call3(21, (uintptr_t)&event_query, sizeof(event_query), 0);
    if (e < 0 || event_query.version != NOS_EVENT_VERSION || event_query.count > NOS_EVENT_BATCH ||
        event_query.next < event_cursor) {
        if (!event_error) {
            say("DESKTOP EVENT OBSERVER ERROR\n");
            redraw = true;
        }
        event_error = true;
        return;
    }
    if (event_error || old_flags != event_query.flags)
        redraw = true;
    event_error = false;
    for (unsigned i = 0; i < event_query.count; ++i) {
        unsigned slot = (event_head + event_count) % NOS_EVENT_RETAINED;
        if (event_count == NOS_EVENT_RETAINED) {
            slot = event_head;
            event_head = (event_head + 1) % NOS_EVENT_RETAINED;
        } else
            ++event_count;
        event_cache[slot] = event_query.events[i];
        if (event_offset)
            ++event_offset;
    }
    event_cursor = event_query.next;
    event_lost += event_query.lost;
    if (event_query.lost)
        say("DESKTOP EVENT RECORDS MISSED\n");
    unsigned limit = event_count > 12 ? event_count - 12 : 0;
    if (event_offset > limit)
        event_offset = limit;
    if (event_query.count || event_query.lost) {
        redraw = true;
        say("DESKTOP EVENTS UPDATED\n");
    }
}
static void events_move(int delta) {
    unsigned limit = event_count > 12 ? event_count - 12 : 0;
    if (delta < 0)
        event_offset = event_offset > (unsigned)-delta ? event_offset + delta : 0;
    else
        event_offset += (unsigned)delta;
    if (event_offset > limit)
        event_offset = limit;
    redraw = true;
}
static const char *event_name(unsigned kind) {
    static const char *names[] = {"Unknown",        "Requested",      "Started",     "Load failed",
                                  "Stop requested", "Exited",         "Fault",       "CPU limit",
                                  "Cancelled",      "Files closed",   "FS recovery", "FS recovered",
                                  "FS unavailable", "Result cleared", "Owner closed"};
    return kind < sizeof(names) / sizeof(names[0]) ? names[kind] : names[0];
}
static void event_number(int x, int y, uint64_t value, unsigned limit, bool signed_value) {
    char value_text[24];
    if (signed_value)
        snprintf(value_text, sizeof(value_text), "%lld", (long long)value);
    else
        snprintf(value_text, sizeof(value_text), "%llu", (unsigned long long)value);
    if (strlen(value_text) > limit) {
        value_text[limit - 1] = '+';
        value_text[limit] = 0;
    }
    text(x, y, value_text, 1, INK);
}
static void draw_events(struct window *w) {
    int x = w->x + 20, y = w->y + 55;
    text(x, y, "Seq", 1, MUTED);
    text(x + 70, y, "Job", 1, MUTED);
    text(x + 140, y, "Event", 1, MUTED);
    text(x + 290, y, "Value", 1, MUTED);
    text(x + 420, y, "Pages", 1, MUTED);
    text(x + 480, y, "CPU", 1, MUTED);
    rect(x, y + 24, w->w - 40, 222, BG);
    for (unsigned row = 0; row < 12 && row + event_offset < event_count; ++row) {
        unsigned index = (event_head + event_count - 1 - event_offset - row) % NOS_EVENT_RETAINED;
        const struct nos_event *event = &event_cache[index];
        int top = y + 34 + (int)row * 18;
        event_number(x + 6, top, event->sequence, 10, false);
        event_number(x + 70, top, event->job, 10, false);
        char name[24];
        snprintf(name, sizeof(name), "%s%s", event_name(event->kind),
                 (event->kind == NOS_EVENT_REQUESTED || event->kind == NOS_EVENT_STARTED) &&
                         (event->flags & NOS_LAUNCH_FILES)
                     ? " +files"
                     : "");
        text(x + 140, top, name, 1, INK);
        event_number(x + 290, top, event->value, 20,
                     event->kind == NOS_EVENT_LOAD_FAILED ||
                         event->kind == NOS_EVENT_FS_RECOVERY_FAILED);
        event_number(x + 420, top, event->pages, 6, false);
        event_number(x + 480, top, event->cpu_ticks, 12, false);
    }
    if (!event_count)
        text(x + 12, y + 40, "No task events yet.", 1, MUTED);
    char status[96];
    snprintf(status, sizeof(status), "Retained %u / missed %llu / newest first", event_count,
             (unsigned long long)event_lost);
    text(x, w->y + w->h - 84, event_error ? "Event observer unavailable." : status, 1, MUTED);
    text(x, w->y + w->h - 66,
         event_query.flags & NOS_EVENT_EXHAUSTED
             ? "Sequence exhausted. Older records remain readable."
             : "Pages: peak owned / CPU: charged ticks / FS closed value 1: rollback",
         1, MUTED);
    button(x, w->y + w->h - 40, 94, "Latest", false);
    button(x + 400, w->y + w->h - 40, 76, "Up", false);
    button(x + 486, w->y + w->h - 40, 76, "Down", false);
}
static void events_key(uint32_t key) {
    if (key == KEY_UP)
        events_move(-1);
    else if (key == KEY_DOWN)
        events_move(1);
    else if (key == KEY_PAGEUP)
        events_move(-12);
    else if (key == KEY_PAGEDOWN)
        events_move(12);
    else if (key == KEY_HOME) {
        event_offset = 0;
        redraw = true;
    } else if (key == KEY_END) {
        event_offset = event_count;
        events_move(0);
    } else if (key == 27) {
        windows[EVENTS].open = false;
        front(PROGRAMS);
    }
}
static void ai_action(bool unload) {
    struct ai_request request = {.op = unload ? AI_UNLOAD : AI_GENERATE, .steps = 48};
    snprintf(request.prompt, sizeof(request.prompt), "%s", ai_prompt);
    if (unload) {
        long e = call3(15, ai_busy ? AI_JOB_CANCEL : AI_JOB_UNLOAD, 0, 0);
        if (e < 0) {
            message("Could not release the AI process. Try again.");
            return;
        }
        say(ai_busy ? "AI GUI CANCELLED\n" : "AI GUI UNLOADED\n");
        message(ai_busy ? "Generation cancelled. Its process memory was released."
                        : "AI model unloaded. Its process memory was released.");
        ai_busy = ai_has_result = false;
        return;
    }
    if (ai_busy) {
        message("Generation is running. You can keep working, or click Cancel.");
        return;
    }
    long e = call3(15, AI_JOB_START, (uintptr_t)&request, sizeof(request));
    if (e < 0) {
        message("AI request could not start. Your notes are preserved.");
        say("AI GUI ERROR\n");
        return;
    }
    ai_busy = true;
    ai_has_result = false;
    ai_scroll = 0;
    message("Generating in the background. Notes and files remain available.");
    say("AI GUI STARTED\n");
}
static void ai_poll(void) {
    if (!ai_busy)
        return;
    long e = call3(15, AI_JOB_POLL, (uintptr_t)&ai_result, sizeof(ai_result));
    if (!e)
        return;
    ai_busy = false;
    if (e < 0 || ai_result.status) {
        message("AI request failed. Desktop and notes are preserved.");
        say("AI GUI ERROR\n");
        return;
    }
    ai_has_result = true;
    message("Generated offline. This test model has no access to your files.");
    say("AI GUI COMPLETE\n");
}
static void ai_key(uint32_t key) {
    if (key == 27 && ai_busy) {
        ai_action(true);
        return;
    }
    unsigned length = (unsigned)strlen(ai_prompt);
    if (key == '\n') {
        ai_action(false);
        return;
    }
    if (key == '\b' && length)
        ai_prompt[length - 1] = 0;
    else if (key >= 32 && key <= 126 && length < 55) {
        ai_prompt[length] = (char)key;
        ai_prompt[length + 1] = 0;
    } else if (key > 127)
        message("The tiny test model uses English prompts. Switch to EN above.");
    redraw = true;
}
static unsigned ai_text_layout(struct window *w, bool draw) {
    unsigned row = 0, col = 0;
    int x = w->x + 28, y = w->y + 172;
    for (unsigned i = 0; i < ai_result.bytes; ++i) {
        char ch = ai_result.text[i];
        if (ch == '\n') {
            ++row;
            col = 0;
            continue;
        }
        if (ch == ' ' && !col)
            continue;
        if (col && ch != ' ' &&
            (!i || ai_result.text[i - 1] == ' ' || ai_result.text[i - 1] == '\n')) {
            unsigned word = 0;
            while (i + word < ai_result.bytes && ai_result.text[i + word] != ' ' &&
                   ai_result.text[i + word] != '\n')
                ++word;
            if (word <= 46 && col + word > 46) {
                ++row;
                col = 0;
            }
        }
        if (draw && row >= ai_scroll && row < ai_scroll + 11) {
            char piece[2] = {ch, 0};
            text(x + (int)col * 12, y + (int)(row - ai_scroll) * 17, piece, 2, INK);
        }
        if (++col == 46) {
            ++row;
            col = 0;
        }
    }
    return row + 1;
}
static void draw_ai(struct window *w) {
    int x = w->x + 20, y = w->y + 50;
    text(x, y + 4, "English continuation model / experimental / not a chat assistant", 1, MUTED);
    rect(x, y + 28, w->w - 40, 30, BG);
    text(x + 10, y + 39, ai_prompt, 1, INK);
    unsigned length = (unsigned)strlen(ai_prompt);
    rect(x + 10 + (int)length * 6, y + 35, 1, 15, accent);
    button(x, y + 70, 110, ai_busy ? "Running..." : "Generate", true);
    button(x + 122, y + 70, 100, ai_busy ? "Cancel" : "Unload", false);
    rect(x, y + 112, w->w - 40, 200, BG);
    if (ai_has_result) {
        (void)ai_text_layout(w, true);
    } else
        text(x + 12, y + 132,
             ai_busy ? "Generating. You can use the other windows." : "No generated text yet.", 1,
             MUTED);
    text(x, y + 337, "Ctrl+Alt+G: generate from any window / Esc: cancel", 1, MUTED);
    button(x + 400, y + 324, 76, "Up", false);
    button(x + 486, y + 324, 76, "Down", false);
}
static void draw_home(struct window *w) {
    int x = w->x + 26, y = w->y + 68;
    text(x, y, "Your space.", 3, INK);
    text(x, y + 32, "Write. Build. Explore.", 2, accent);
    text(x, y + 75, "Notes in Russian or English. Your files on disk.", 1, MUTED);
    text(x, y + 94, "Run native programs without leaving your desktop.", 1, MUTED);
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
    text(x, y + 280, "PREVIEW " NEUROOS_VERSION "  /  RU-EN in the top bar", 1, MUTED);
    text(x, y + 304, "Ctrl+S saves. Apps opens native .nos programs.", 1, MUTED);
}
static void draw_files(struct window *w) {
    int x = w->x + 20, y = w->y + 50;
    button(x, y, 100, "New note", true);
    button(x + 112, y, 90, "Refresh", false);
    button(x + 214, y, 110, "Open / Run", false);
    button(x + 338, y, 96, "Build C", false);
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
    rect(bx, by, w->w - 40, w->h - 156, 0x0c1722);
    unsigned cr, cc;
    cursor_position(cursor, &cr, &cc);
    if (cr < note_scroll)
        note_scroll = cr;
    if (cr >= note_scroll + note_rows())
        note_scroll = cr - note_rows() + 1;
    unsigned row = 0, col = 0, first, count = text_selection(&document, &first);
    for (unsigned i = 0; i < note_len;) {
        uint32_t cp;
        unsigned n = utf8_decode(note + i, note_len - i, &cp);
        if (!n)
            break;
        if (cp == '\r') {
            i += n;
            continue;
        }
        if (cp == '\n') {
            if (i >= first && i < first + count && row >= note_scroll &&
                row < note_scroll + note_rows())
                rect(bx + 8 + (int)col * 12, by + 8 + (int)(row - note_scroll) * 17, 12, 17, EDGE);
            ++row;
            col = 0;
            i += n;
            continue;
        }
        unsigned cells = cp == '\t' ? 4 - col % 4 : 1;
        for (unsigned cell = 0; cell < cells; ++cell) {
            if (row >= note_scroll && row < note_scroll + note_rows()) {
                int px = bx + 8 + (int)col * 12, py = by + 9 + (int)(row - note_scroll) * 17;
                if (i >= first && i < first + count)
                    rect(px, py - 1, 12, 17, 0x365b79);
                if (cp != '\t') {
                    char bytes[5] = {0};
                    utf8_encode(cp, bytes);
                    text(px, py, bytes, 2, INK);
                }
            }
            if (++col == note_columns()) {
                ++row;
                col = 0;
            }
        }
        i += n;
    }
    if (!name_focus)
        rect(bx + 8 + (int)cc * 12, by + 9 + (int)(cr - note_scroll) * 17, 2, 14, accent);
    char status[70];
    snprintf(status, sizeof(status), "%u / 8192 bytes  UTF-8  Ctrl+S save / Z undo / L name",
             note_len);
    text(x, y + w->h - 92, status, 1, MUTED);
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
    text(x, y + 191, "UTF-8 notes / RU-EN: Alt+Shift or top bar", 1, MUTED);
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
        if (id == NOTES)
            button(w->x + w->w - 76, w->y + 4, 30, note_maximized ? "-" : "+", false);
        if (id == PROGRAMS)
            button(w->x + w->w - 140, w->y + 4, 95, "Events", false);
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
        if (id == PROGRAMS)
            draw_programs(w);
        if (id == AI)
            draw_ai(w);
        if (id == EVENTS)
            draw_events(w);
    }
    rect(0, 533, W, 67, 0x111e2b);
    line(0, 533, W, EDGE);
    for (int id = 0; id <= PROGRAMS; ++id) {
        int x = 18 + id * 108;
        if (windows[id].open)
            rect(x, 542, 96, 47, id == active ? EDGE : PANEL);
        icon(id, x + 8, 547, accent);
        text(x + 43, 558, labels[id], 1, INK);
    }
#if NEUROOS_AI_DEMO
    button(414, 3, 96, "AI demo", false);
#endif
    button(525, 3, 70, layout.russian ? "RU" : "EN", false);
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
#if NEUROOS_AI_DEMO
    if (!dialog && inside(414, 3, 96, 28)) {
        front(AI);
        return;
    }
#endif
    if (!dialog && inside(525, 3, 70, 28)) {
        layout.russian = !layout.russian;
        redraw = true;
        return;
    }
    if (dialog) {
        if (inside(280, 292, 120, 28))
            dialog = NO_DIALOG;
        else if (inside(418, 292, 175, 28))
            confirm();
        redraw = true;
        return;
    }
    if (input.y >= 533) {
        for (int i = 0; i <= PROGRAMS; ++i)
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
        if (id == NOTES && inside(w->x + w->w - 76, w->y + 4, 30, 28)) {
            if (note_maximized) {
                *w = note_restore;
                note_maximized = false;
            } else {
                note_restore = *w;
                *w = (struct window){0, 35, 800, 480, true};
                note_maximized = true;
            }
            redraw = true;
            return;
        }
        if (id == PROGRAMS && inside(w->x + w->w - 140, w->y + 4, 95, 28)) {
            front(EVENTS);
            return;
        }
        if (input.y < w->y + 35) {
            if (id == NOTES && note_maximized)
                return;
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
                open_file(files[selected].name);
            else if (inside(x + 338, y, 96, 28) && selected >= 0)
                build_source(files[selected].name);
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
            else if (inside(x, y + 44, w->w - 40, w->h - 156)) {
                name_focus = false;
                unsigned targetrow =
                    note_scroll + (unsigned)(input.y < y + 53 ? 0 : input.y - (y + 53)) / 17;
                unsigned targetcol = (unsigned)(input.x - x < 8 ? 0 : input.x - x - 8) / 12;
                cursor = text_hit(&document, targetrow, targetcol, note_columns());
                if (!(input.modifiers & INPUT_SHIFT))
                    document.now.anchor = cursor;
                selecting = true;
            }
        } else if (id == AI) {
            if (inside(x, y + 70, 110, 28))
                ai_action(false);
            else if (inside(x + 122, y + 70, 100, 28))
                ai_action(true);
            else if (inside(x + 400, y + 324, 76, 28))
                ai_scroll = ai_scroll > 10 ? ai_scroll - 10 : 0;
            else if (inside(x + 486, y + 324, 76, 28)) {
                unsigned rows = ai_text_layout(w, false);
                unsigned limit = rows > 11 ? rows - 11 : 0;
                ai_scroll += 10;
                if (ai_scroll > limit)
                    ai_scroll = limit;
            }
        } else if (id == PROGRAMS) {
            if (inside(x + 38, y, 185, 28))
                program_arguments_focus = false;
            else if (inside(x + 38, y + 39, w->w - 78, 28))
                program_arguments_focus = true;
            else if (inside(x + 237, y, 80, 28))
                run_program();
            else if (inside(x + 330, y, 245, 28)) {
                if (!program_settings_locked())
                    program_files = !program_files;
            } else if (inside(x + 400, y + 326, 76, 28))
                program_scroll = program_scroll > 15 ? program_scroll - 15 : 0;
            else if (inside(x + 486, y + 326, 76, 28)) {
                unsigned rows = output_rows(), limit = rows > 18 ? rows - 18 : 0;
                program_scroll += 15;
                if (program_scroll > limit)
                    program_scroll = limit;
            }
        } else if (id == EVENTS) {
            if (inside(x, w->y + w->h - 40, 94, 28)) {
                event_offset = 0;
                redraw = true;
            } else if (inside(x + 400, w->y + w->h - 40, 76, 28))
                events_move(-12);
            else if (inside(x + 486, w->y + w->h - 40, 76, 28))
                events_move(12);
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
    const char *greeting =
        "Hello from NeuroOS!\n\nThis is a real note. Edit me, then click Save.\n";
    (void)text_load(&document, greeting, (unsigned)strlen(greeting));
    dirty = true;
    say("DESKTOP READY\n");
    struct desktop_input old = {.x = -1, .y = -1};
    while (!exiting) {
        program_poll();
        events_poll();
        ai_poll();
        if (call3(11, 0, (uintptr_t)&input, 0) < 0) {
            free(pixels);
            return 2;
        }
        if (input.x != old.x || input.y != old.y || input.buttons != old.buttons)
            redraw = true;
        if ((input.buttons & 1) && !(old.buttons & 1))
            click();
        if (!(input.buttons & 1)) {
            drag = -1;
            selecting = false;
        }
        if (selecting && (input.buttons & 1) && active == NOTES) {
            struct window *w = &windows[NOTES];
            int x = w->x + 28, y = w->y + 101;
            unsigned row = note_scroll + (unsigned)(input.y < y ? 0 : input.y - y) / 17;
            unsigned col = (unsigned)(input.x < x ? 0 : input.x - x) / 12;
            if (col >= note_columns())
                col = note_columns() - 1;
            cursor = text_hit(&document, row, col, note_columns());
            redraw = true;
        }
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
        bool was_ru = layout.russian;
        uint32_t key = keyboard_translate(&layout, &input);
        if (was_ru != layout.russian)
            redraw = true;
        if (!dialog && input.scancode == 0x10 &&
            (input.modifiers & (INPUT_CONTROL | INPUT_ALT)) == (INPUT_CONTROL | INPUT_ALT))
            request_exit();
#if NEUROOS_AI_DEMO
        if (!dialog && input.scancode == 0x22 &&
            (input.modifiers & (INPUT_CONTROL | INPUT_ALT)) == (INPUT_CONTROL | INPUT_ALT))
            ai_action(false);
#endif
        if (key) {
            if (dialog) {
                if (key == 27)
                    dialog = NO_DIALOG;
                redraw = true;
            } else if (windows[active].open && active == NOTES)
                note_key(key);
            else if (windows[active].open && active == EVENTS)
                events_key(key);
            else if (windows[active].open && active == AI)
                ai_key(key);
            else if (windows[active].open && active == PROGRAMS)
                program_key(key);
            else if (windows[active].open && active == CALC)
                calc_key(key < 128 ? (char)key : 0);
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

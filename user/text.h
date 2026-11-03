#ifndef NEUROOS_TEXT_H
#define NEUROOS_TEXT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define TEXT_LIMIT 8192u
#define TEXT_HISTORY 9u
#define KEY_UP 0x110001u
#define KEY_DOWN 0x110002u
#define KEY_LEFT 0x110003u
#define KEY_RIGHT 0x110004u
#define KEY_HOME 0x110005u
#define KEY_END 0x110006u
#define KEY_DELETE 0x110007u
#define KEY_PAGEUP 0x110008u
#define KEY_PAGEDOWN 0x110009u
struct text_snapshot {
    char data[TEXT_LIMIT + 1];
    unsigned length, cursor, anchor;
};
struct text_document {
    struct text_snapshot now, history[TEXT_HISTORY];
    unsigned history_count, history_position;
};
unsigned utf8_decode(const char *data, unsigned bytes, uint32_t *cp);
unsigned utf8_encode(uint32_t cp, char out[4]);
bool text_valid(const char *data, unsigned bytes);
unsigned text_next(const char *data, unsigned length, unsigned offset);
unsigned text_previous(const char *data, unsigned offset);
bool text_load(struct text_document *d, const char *data, unsigned bytes);
bool text_insert(struct text_document *d, const char *data, unsigned bytes);
bool text_codepoint(struct text_document *d, uint32_t cp);
bool text_erase(struct text_document *d, bool backwards);
bool text_undo(struct text_document *d, bool redo);
unsigned text_selection(const struct text_document *d, unsigned *first);
void text_position(const struct text_document *d, unsigned offset, unsigned columns, unsigned *row,
                   unsigned *column);
unsigned text_hit(const struct text_document *d, unsigned row, unsigned column, unsigned columns);
void text_move(struct text_document *d, uint32_t key, bool extend, bool control, unsigned columns);
#endif

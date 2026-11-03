#include "text.h"
#include "libc/string.h"
unsigned utf8_decode(const char *data, unsigned bytes, uint32_t *cp) {
    if (!bytes)
        return 0;
    const unsigned char *p = (const unsigned char *)data;
    unsigned n;
    uint32_t c, minimum;
    if (p[0] < 0x80) {
        *cp = p[0];
        return 1;
    }
    if (p[0] >= 0xc2 && p[0] <= 0xdf) {
        n = 2;
        c = p[0] & 31;
        minimum = 0x80;
    } else if (p[0] >= 0xe0 && p[0] <= 0xef) {
        n = 3;
        c = p[0] & 15;
        minimum = 0x800;
    } else if (p[0] >= 0xf0 && p[0] <= 0xf4) {
        n = 4;
        c = p[0] & 7;
        minimum = 0x10000;
    } else
        return 0;
    if (bytes < n)
        return 0;
    for (unsigned i = 1; i < n; ++i) {
        if ((p[i] & 0xc0) != 0x80)
            return 0;
        c = (c << 6) | (p[i] & 63);
    }
    if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
        return 0;
    *cp = c;
    return n;
}
unsigned utf8_encode(uint32_t cp, char out[4]) {
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        return 0;
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 63));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xe0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 63));
        out[2] = (char)(0x80 | (cp & 63));
        return 3;
    }
    out[0] = (char)(0xf0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 63));
    out[2] = (char)(0x80 | ((cp >> 6) & 63));
    out[3] = (char)(0x80 | (cp & 63));
    return 4;
}
bool text_valid(const char *data, unsigned bytes) {
    for (unsigned i = 0; i < bytes;) {
        uint32_t cp;
        unsigned n = utf8_decode(data + i, bytes - i, &cp);
        if (!n || cp == 0 || (cp < 32 && cp != '\n' && cp != '\t' && cp != '\r') ||
            (cp >= 127 && cp <= 159))
            return false;
        i += n;
    }
    return true;
}
unsigned text_next(const char *data, unsigned length, unsigned offset) {
    if (offset >= length)
        return length;
    uint32_t cp;
    unsigned n = utf8_decode(data + offset, length - offset, &cp);
    return offset + (n ? n : 1);
}
unsigned text_previous(const char *data, unsigned offset) {
    if (!offset)
        return 0;
    --offset;
    while (offset && ((unsigned char)data[offset] & 0xc0) == 0x80)
        --offset;
    return offset;
}
static void remember(struct text_document *d) {
    unsigned next = d->history_position + 1;
    if (next == TEXT_HISTORY) {
        memmove(d->history, d->history + 1, (TEXT_HISTORY - 1) * sizeof(d->history[0]));
        --next;
    }
    d->history[next] = d->now;
    d->history_position = next;
    d->history_count = next + 1;
}
bool text_load(struct text_document *d, const char *data, unsigned bytes) {
    if (bytes > TEXT_LIMIT || !text_valid(data, bytes))
        return false;
    memcpy(d->now.data, data, bytes);
    d->now.data[bytes] = 0;
    d->now.length = bytes;
    d->now.cursor = d->now.anchor = bytes;
    d->history[0] = d->now;
    d->history_position = 0;
    d->history_count = 1;
    return true;
}
unsigned text_selection(const struct text_document *d, unsigned *first) {
    unsigned a = d->now.anchor, b = d->now.cursor;
    *first = a < b ? a : b;
    return a > b ? a - b : b - a;
}
bool text_insert(struct text_document *d, const char *data, unsigned bytes) {
    unsigned first, count = text_selection(d, &first);
    if (bytes > TEXT_LIMIT - (d->now.length - count) || !text_valid(data, bytes))
        return false;
    if (!bytes && !count)
        return true;
    d->history[d->history_position] = d->now;
    /* Clipboard is external to the document; callers never alias this buffer. */
    memmove(d->now.data + first + bytes, d->now.data + first + count,
            d->now.length - first - count + 1);
    memcpy(d->now.data + first, data, bytes);
    d->now.length = d->now.length - count + bytes;
    d->now.cursor = d->now.anchor = first + bytes;
    remember(d);
    return true;
}
bool text_codepoint(struct text_document *d, uint32_t cp) {
    char bytes[4];
    unsigned n = utf8_encode(cp, bytes);
    return n && text_insert(d, bytes, n);
}
bool text_erase(struct text_document *d, bool backwards) {
    unsigned first, count = text_selection(d, &first);
    if (!count) {
        if (backwards)
            d->now.anchor = text_previous(d->now.data, d->now.cursor);
        else
            d->now.anchor = text_next(d->now.data, d->now.length, d->now.cursor);
    }
    return text_insert(d, "", 0);
}
bool text_undo(struct text_document *d, bool redo) {
    if (redo) {
        if (d->history_position + 1 >= d->history_count)
            return false;
        ++d->history_position;
    } else {
        if (!d->history_position)
            return false;
        --d->history_position;
    }
    d->now = d->history[d->history_position];
    return true;
}
static void advance(uint32_t cp, unsigned columns, unsigned *r, unsigned *c) {
    if (cp == '\r')
        return;
    if (cp == '\n') {
        ++*r;
        *c = 0;
        return;
    }
    unsigned width = cp == '\t' ? 4 - (*c % 4) : 1;
    while (width--) {
        if (++*c == columns) {
            ++*r;
            *c = 0;
        }
    }
}
void text_position(const struct text_document *d, unsigned offset, unsigned columns, unsigned *row,
                   unsigned *column) {
    unsigned r = 0, c = 0;
    if (!columns)
        columns = 1;
    if (offset > d->now.length)
        offset = d->now.length;
    for (unsigned i = 0; i < offset;) {
        uint32_t cp;
        unsigned n = utf8_decode(d->now.data + i, d->now.length - i, &cp);
        if (!n)
            break;
        advance(cp, columns, &r, &c);
        i += n;
    }
    *row = r;
    *column = c;
}
unsigned text_hit(const struct text_document *d, unsigned row, unsigned column, unsigned columns) {
    unsigned r = 0, c = 0;
    if (!columns)
        columns = 1;
    for (unsigned i = 0; i < d->now.length;) {
        uint32_t cp;
        unsigned n = utf8_decode(d->now.data + i, d->now.length - i, &cp);
        if (!n)
            return i;
        if (r > row || (r == row && (c >= column || cp == '\n')))
            return i;
        unsigned nr = r, nc = c;
        advance(cp, columns, &nr, &nc);
        if (r == row && nr == r && nc > column)
            return i;
        r = nr;
        c = nc;
        i += n;
    }
    return d->now.length;
}
void text_move(struct text_document *d, uint32_t key, bool extend, bool control, unsigned columns) {
    unsigned pos = d->now.cursor, r, c, first, count = text_selection(d, &first);
    text_position(d, pos, columns, &r, &c);
    if (key == KEY_LEFT)
        pos = !extend && count ? first : text_previous(d->now.data, pos);
    else if (key == KEY_RIGHT)
        pos = !extend && count ? first + count : text_next(d->now.data, d->now.length, pos);
    else if (key == KEY_UP)
        pos = r ? text_hit(d, r - 1, c, columns) : 0;
    else if (key == KEY_DOWN)
        pos = text_hit(d, r + 1, c, columns);
    else if (key == KEY_PAGEUP)
        pos = text_hit(d, r > 14 ? r - 14 : 0, c, columns);
    else if (key == KEY_PAGEDOWN)
        pos = text_hit(d, r + 14, c, columns);
    else if (key == KEY_HOME)
        pos = control ? 0 : text_hit(d, r, 0, columns);
    else if (key == KEY_END)
        pos = control ? d->now.length : text_hit(d, r, columns, columns);
    d->now.cursor = pos;
    if (!extend)
        d->now.anchor = pos;
}

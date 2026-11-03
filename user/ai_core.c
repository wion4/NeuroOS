/* Native isolated inference prototype; upstream neural blocks retain MIT terms. */
#include "ai.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include <stddef.h>
#include <stdint.h>
float ai_sqrtf(float), ai_expf(float), ai_powf(float, float), ai_sinf(float), ai_cosf(float);
#ifdef NEUROOS_AI_ORACLE
float sqrtf(float), expf(float), powf(float, float), sinf(float), cosf(float);
#else
#define sqrtf ai_sqrtf
#define expf ai_expf
#define powf ai_powf
#define sinf ai_sinf
#define cosf ai_cosf
#endif
static void *ai_bsearch(const void *key, const void *base, size_t count, size_t size,
                        int (*compare)(const void *, const void *)) {
    const unsigned char *bytes = base;
    while (count) {
        size_t half = count / 2;
        const void *mid = bytes + half * size;
        int c = compare(key, mid);
        if (!c)
            return (void *)mid;
        if (c < 0)
            count = half;
        else {
            bytes = (const unsigned char *)mid + size;
            count -= half + 1;
        }
    }
    return NULL;
}
#include "ai-upstream.h"
static Transformer transformer;
static Tokenizer tokenizer;
static int ready;
static uint32_t read32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t crc32(const unsigned char *data, unsigned bytes) {
    uint32_t crc = ~0u;
    for (unsigned i = 0; i < bytes; ++i) {
        crc ^= data[i];
        for (unsigned j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
    }
    return ~crc;
}
void ai_core_release(void) {
    RunState *s = &transformer.state;
    free(s->x);
    free(s->xb);
    free(s->xb2);
    free(s->hb);
    free(s->hb2);
    free(s->q);
    free(s->att);
    free(s->logits);
    free(s->key_cache);
    free(s->value_cache);
    if (tokenizer.vocab)
        for (unsigned i = 0; i < 512; ++i)
            free(tokenizer.vocab[i]);
    free(tokenizer.vocab);
    free(tokenizer.vocab_scores);
    free(tokenizer.sorted_vocab);
    memset(&transformer, 0, sizeof(transformer));
    memset(&tokenizer, 0, sizeof(tokenizer));
    ready = 0;
}
int ai_core_init(const void *pack, unsigned bytes) {
    ai_core_release();
    const unsigned char *p = pack;
    if (!pack || bytes < 32 || memcmp(p, "NOSAI001", 8) || read32(p + 8) != 1056540 ||
        read32(p + 12) != 6227 || read32(p + 24) || read32(p + 28) || bytes != 32 + 1056540 + 6227)
        return -22;
    if (crc32(p + 32, 1056540) != read32(p + 16) || crc32(p + 32 + 1056540, 6227) != read32(p + 20))
        return -5;
    const int config[7] = {64, 172, 5, 8, 4, 512, 512};
    _Static_assert(sizeof(Config) == 28, "Pinned checkpoint config");
    if (memcmp(p + 32, config, sizeof(config)))
        return -22;
    memcpy(&transformer.config, p + 32, 28);
    memory_map_weights(&transformer.weights, &transformer.config, (float *)(void *)(p + 60), 1);
    /* Reject nonfinite parameters even when a caller supplied a matching CRC. */
    for (unsigned i = 60; i < 32 + 1056540; i += 4)
        if ((read32(p + i) & 0x7f800000u) == 0x7f800000u)
            return -22;
    RunState *s = &transformer.state;
    s->x = calloc(64, 4);
    s->xb = calloc(64, 4);
    s->xb2 = calloc(64, 4);
    s->q = calloc(64, 4);
    s->hb = calloc(172, 4);
    s->hb2 = calloc(172, 4);
    s->att = calloc(8 * 512, 4);
    s->logits = calloc(512, 4);
    s->key_cache = calloc(5 * 512 * 32, 4);
    s->value_cache = calloc(5 * 512 * 32, 4);
    if (!s->x || !s->xb || !s->xb2 || !s->q || !s->hb || !s->hb2 || !s->att || !s->logits ||
        !s->key_cache || !s->value_cache)
        goto oom;
    tokenizer.vocab = calloc(512, sizeof(char *));
    tokenizer.vocab_scores = malloc(512 * 4);
    if (!tokenizer.vocab || !tokenizer.vocab_scores)
        goto oom;
    tokenizer.vocab_size = 512;
    const unsigned char *tokens = p + 32 + 1056540;
    tokenizer.max_token_length = read32(tokens);
    if (!tokenizer.max_token_length || tokenizer.max_token_length > 64)
        goto invalid;
    unsigned offset = 4;
    for (unsigned i = 0; i < 256; ++i) {
        tokenizer.byte_pieces[2 * i] = (unsigned char)i;
        tokenizer.byte_pieces[2 * i + 1] = 0;
    }
    for (unsigned i = 0; i < 512; ++i) {
        if (offset > 6227 - 8)
            goto invalid;
        uint32_t score = read32(tokens + offset), len = read32(tokens + offset + 4);
        offset += 8;
        if ((score & 0x7f800000u) == 0x7f800000u || !len || len > tokenizer.max_token_length ||
            len > 6227 - offset)
            goto invalid;
        memcpy(&tokenizer.vocab_scores[i], &score, 4);
        tokenizer.vocab[i] = malloc(len + 1);
        if (!tokenizer.vocab[i])
            goto oom;
        memcpy(tokenizer.vocab[i], tokens + offset, len);
        tokenizer.vocab[i][len] = 0;
        offset += len;
    }
    if (offset != 6227)
        goto invalid;
    tokenizer.sorted_vocab = malloc(512 * sizeof(TokenIndex));
    if (!tokenizer.sorted_vocab)
        goto oom;
    for (unsigned i = 0; i < 512; ++i) {
        tokenizer.sorted_vocab[i].str = tokenizer.vocab[i];
        tokenizer.sorted_vocab[i].id = (int)i;
    }
    qsort(tokenizer.sorted_vocab, 512, sizeof(TokenIndex), compare_tokens);
    if (str_lookup(" ", tokenizer.sorted_vocab, 512) < 0)
        goto invalid;
    ready = 1;
    return 0;
oom:
    ai_core_release();
    return -12;
invalid:
    ai_core_release();
    return -22;
}
static const char *piece(int previous, int token) {
    const char *p = tokenizer.vocab[token];
    if (previous == 1 && p[0] == ' ')
        ++p;
    if (strlen(p) == 6 && p[0] == '<' && p[1] == '0' && p[2] == 'x' && p[5] == '>') {
        unsigned value = 0;
        for (unsigned i = 3; i < 5; ++i) {
            unsigned c = (unsigned char)p[i], v = c >= '0' && c <= '9'   ? c - '0'
                                                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                                         : 99;
            if (v > 15)
                return p;
            value = value * 16 + v;
        }
        return (const char *)tokenizer.byte_pieces + value * 2;
    }
    return p;
}
int ai_core_generate(const char *prompt, unsigned steps, char *output, unsigned capacity,
                     uint32_t *trace, unsigned *trace_count) {
    if (!ready || !prompt || !output || !capacity || !trace || !trace_count || !steps ||
        steps > AI_STEPS)
        return -22;
    size_t length = 0;
    while (length < 56 && prompt[length])
        ++length;
    if (length > 55)
        return -22;
    for (unsigned i = 0; i < length; ++i)
        if ((unsigned char)prompt[i] < 32 || (unsigned char)prompt[i] > 126)
            return -22;
    int prompt_tokens[60], count = 0;
    encode(&tokenizer, (char *)prompt, 1, 0, prompt_tokens, &count);
    if (count < 1 || count > (int)steps)
        return -22;
    unsigned bytes = 0;
    *trace_count = 0;
    output[0] = 0;
    int token = prompt_tokens[0];
    for (unsigned pos = 0; pos < steps; ++pos) {
        if (token < 0 || token >= 512)
            return -22;
        float *logits = forward(&transformer, token, (int)pos);
        int next = 0;
        if (pos + 1 < (unsigned)count)
            next = prompt_tokens[pos + 1];
        else
            for (int j = 1; j < 512; ++j)
                if (logits[j] > logits[next])
                    next = j;
        trace[(*trace_count)++] = (uint32_t)next;
        if (next == 1)
            break;
        const char *text = piece(token, next);
        while (*text) {
            unsigned char c = (unsigned char)*text++;
            if ((c < 32 && c != '\n' && c != '\t' && c != '\r') || c == 127)
                continue;
            if (bytes + 1 >= capacity)
                return -28;
            output[bytes++] = (char)c;
        }
        output[bytes] = 0;
        token = next;
    }
    return (int)bytes;
}

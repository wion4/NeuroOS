#include "abi.h"
#include "ai.h"
#include "heap.h"
#include "libc/string.h"
static int reply(struct ai_reply *r) {
    long n;
    while ((n = send_message(2, r, sizeof(*r))) == -11)
        yield();
    return n < 0 ? -1 : 0;
}
int user_main(void) {
    long size = call3(17, 3, ~0ull, 0);
    if (size != 1062799) {
        say("AI MODEL UNAVAILABLE\n");
        return 10;
    }
    if (call3(17, 1, ~0ull, 0) != -1 || call3(9, 3, 0x1f7, 0) != -1 ||
        send_message(3, "x", 1) != -1)
        return 11;
    if (call3(17, 3, 0, 0x400000) != -14 || call3(17, 3, (uint64_t)size + 1, 0) != -22)
        return 12;
    unsigned char *pack = malloc((size_t)size);
    if (!pack)
        return 13;
    for (unsigned done = 0; done < (unsigned)size;) {
        long n = call3(17, 3, done, (uintptr_t)(pack + done));
        if (n <= 0 || (unsigned long)n > (unsigned)size - done)
            return 14;
        done += (unsigned)n;
    }
    int e = ai_core_init(pack, (unsigned)size);
    if (e) {
        say("AI MODEL REJECTED\n");
        free(pack);
        return 15;
    }
    say("AI RUNTIME READY - READ ONLY MODEL, NO FILE OR DISK RIGHTS\n");
    for (;;) {
        struct ai_request q;
        long n = receive_message(1, &q, sizeof(q));
        if (n == -11) {
            yield();
            continue;
        }
        if (n < 0)
            break;
        struct ai_reply r = {.op = AI_DONE, .status = -22};
        if (n != sizeof(q) || q.op != AI_GENERATE || q.prompt[55]) {
            if (reply(&r))
                break;
            continue;
        }
        char output[AI_TEXT_BYTES];
        uint32_t trace[AI_STEPS];
        unsigned count = 0;
        e = ai_core_generate(q.prompt, q.steps, output, sizeof(output), trace, &count);
        if (e < 0) {
            r.status = e;
            if (reply(&r))
                break;
            continue;
        }
        for (unsigned offset = 0; offset < (unsigned)e;) {
            r = (struct ai_reply){.op = AI_TEXT, .offset = offset, .bytes = (unsigned)e - offset};
            if (r.bytes > sizeof(r.data))
                r.bytes = sizeof(r.data);
            memcpy(r.data, output + offset, r.bytes);
            if (reply(&r))
                return 16;
            offset += r.bytes;
        }
        for (unsigned offset = 0; offset < count;) {
            unsigned amount = count - offset;
            if (amount > 12)
                amount = 12;
            r = (struct ai_reply){.op = AI_TRACE, .offset = offset, .bytes = amount};
            memcpy(r.data, trace + offset, amount * 4);
            if (reply(&r))
                return 17;
            offset += amount;
        }
        r = (struct ai_reply){.op = AI_DONE, .bytes = (unsigned)e, .offset = count};
        if (reply(&r))
            break;
    }
    ai_core_release();
    free(pack);
    return 0;
}

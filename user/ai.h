#ifndef NEUROOS_AI_PROTOCOL_H
#define NEUROOS_AI_PROTOCOL_H
#include <stdint.h>
#define AI_STEPS 96u
#define AI_TEXT_BYTES 1536u
#define AI_GENERATE 1u
#define AI_DONE 2u
#define AI_TEXT 3u
#define AI_TRACE 4u
#define AI_UNLOAD 5u
/* Desktop-only syscall 15. START copies the request; POLL copies a complete
   session only when it returns 1. No user pointer is retained across calls. */
#define AI_JOB_START 0u
#define AI_JOB_POLL 1u
#define AI_JOB_CANCEL 2u
#define AI_JOB_UNLOAD 3u
struct ai_request {
    uint32_t op, steps;
    char prompt[56];
};
struct ai_reply {
    uint32_t op, bytes, offset;
    int32_t status;
    unsigned char data[48];
};
struct ai_session {
    struct ai_request request;
    int32_t status;
    uint32_t bytes, trace_count, reserved;
    char text[AI_TEXT_BYTES];
    uint32_t trace[AI_STEPS];
};
_Static_assert(sizeof(struct ai_request) == 64, "AI request IPC ABI");
_Static_assert(sizeof(struct ai_reply) == 64, "AI reply IPC ABI");
_Static_assert(sizeof(struct ai_session) == 2000, "AI session ABI");
int ai_core_init(const void *pack, unsigned bytes);
void ai_core_release(void);
int ai_core_generate(const char *prompt, unsigned steps, char *output, unsigned capacity,
                     uint32_t *trace, unsigned *trace_count);
#endif

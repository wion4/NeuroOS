#include "abi.h"
#include "ai.h"
#include "libc/string.h"
int user_main(struct ai_session *session) {
    if (!session)
        return 1;
    if (call3(17, 3, ~0ull, 0) != -1)
        return 2;
    long n;
    while ((n = send_message(1, &session->request, sizeof(session->request))) == -11)
        yield();
    if (n < 0)
        return 3;
    session->bytes = session->trace_count = 0;
    session->status = -5;
    for (;;) {
        struct ai_reply r;
        while ((n = receive_message(2, &r, sizeof(r))) == -11)
            yield();
        if (n != sizeof(r))
            return 4;
        if (r.op == AI_TEXT) {
            if (r.offset != session->bytes || r.bytes > 48 ||
                r.bytes >= AI_TEXT_BYTES - session->bytes)
                return 5;
            memcpy(session->text + session->bytes, r.data, r.bytes);
            session->bytes += r.bytes;
        } else if (r.op == AI_TRACE) {
            if (r.offset != session->trace_count || r.bytes > 12 ||
                r.bytes > AI_STEPS - session->trace_count)
                return 6;
            memcpy(session->trace + session->trace_count, r.data, r.bytes * 4);
            session->trace_count += r.bytes;
        } else if (r.op == AI_DONE) {
            if (!r.status && (r.bytes != session->bytes || r.offset != session->trace_count))
                return 7;
            session->status = r.status;
            session->text[session->bytes] = 0;
            return 0;
        } else
            return 8;
    }
}

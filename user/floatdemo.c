#include "abi.h"
#include "libc/errno.h"
#include "libc/stdlib.h"
#include <stdint.h>
int user_main(const char *arguments) {
    (void)arguments;
    char *end;
    errno = 0;
    if (strtod("  -1.25e2tail", &end) != -125.0 || *end != 't' || errno)
        return 100;
    if (strtof("0x1.8p+2!", &end) != 6.0f || *end != '!')
        return 101;
    if (strtold("1.0000000000000000001", 0) <= 1.0L)
        return 102;
    union {
        double value;
        uint64_t bits;
    } number;
    number.value = strtod("0.1", 0);
    if (number.bits != 0x3fb999999999999aull)
        return 103;
    number.value = strtod("2.2250738585072014e-308", 0);
    if (number.bits != 0x0010000000000000ull)
        return 104;
    number.value = strtod("4.9406564584124654e-324", 0);
    if (number.bits != 1)
        return 105;
    number.value = strtod("-0", 0);
    if (number.bits != 0x8000000000000000ull)
        return 106;
    errno = 0;
    number.value = strtod("1e9999", 0);
    if (number.bits != 0x7ff0000000000000ull || errno != ERANGE)
        return 107;
    errno = 0;
    number.value = strtod("1e-9999", 0);
    if (number.bits || errno != ERANGE)
        return 108;
    const char *invalid = "not a number";
    if (strtod(invalid, &end) != 0 || end != invalid)
        return 109;
    if (strtod("2e+", &end) != 2 || *end != 'e')
        return 110;
    number.value = strtod("nan(payload)", &end);
    if (number.value == number.value || *end)
        return 111;
    say("C FLOAT TEST PASS\n");
    return 0;
}

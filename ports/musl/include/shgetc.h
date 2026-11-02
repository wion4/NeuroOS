#ifndef NEUROOS_MUSL_SHGETC_H
#define NEUROOS_MUSL_SHGETC_H
#include <stdio.h>
/* floatscan is used only on NUL-terminated strings, with an unlimited (zero)
 * field width. Preserve musl's count-reset and unlimited pushback semantics. */
#define sh_fromstring(f,s) ((f)->current=(f)->count_from=(const unsigned char *)(s))
#define shlim(f,limit) ((void)(limit),(f)->count_from=(f)->current)
#define shcnt(f) ((f)->current-(f)->count_from)
#define shgetc(f) (*(f)->current++)
#define shunget(f) ((void)--(f)->current)
#endif

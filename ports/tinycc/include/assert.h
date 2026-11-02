#ifndef NEUROOS_TCC_PROBE_ASSERT_H
#define NEUROOS_TCC_PROBE_ASSERT_H
_Noreturn void __assert_fail(const char *, const char *, unsigned, const char *);
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
#define assert(x) ((x) ? (void)0 : __assert_fail(#x,__FILE__,__LINE__,__func__))
#endif
#endif

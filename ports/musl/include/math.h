#ifndef NEUROOS_MUSL_MATH_H
#define NEUROOS_MUSL_MATH_H
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define fabsl(x) __builtin_fabsl(x)
#define copysignl(x,y) __builtin_copysignl(x,y)
long double nos_scale_ld(long double,int);
long double nos_remainder_ld(long double,long double);
#define scalbnl(x,n) nos_scale_ld(x,n)
/* floatscan's double scalbn calls form exact powers of two, not arbitrary
 * double inputs requiring a full public scalbn implementation. */
#define scalbn(x,n) ((double)nos_scale_ld((long double)(x),(n)))
#define fmodl(x,y) nos_remainder_ld(x,y)
#endif

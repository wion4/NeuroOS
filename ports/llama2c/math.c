/* Private bounded x87 math for the demo runtime, not a complete libm. */
_Static_assert(sizeof(long double) >= 10, "x87 helpers require extended long double");
static long double exp2_ld(long double x) {
    long double out;
    __asm__ volatile("fldt %1; fld %%st(0); frndint; fxch %%st(1); "
                     "fsub %%st(1),%%st; f2xm1; fld1; faddp; fscale; fstp %%st(1); fstpt %0"
                     : "=m"(out)
                     : "m"(x)
                     : "st", "st(1)", "st(2)");
    return out;
}
float ai_sqrtf(float x) {
    float result;
    __asm__ volatile("sqrtss %1,%0" : "=x"(result) : "x"(x));
    return result;
}
float ai_expf(float x) {
    if (x != x)
        return x;
    if (x > 88.722839f)
        return __builtin_inff();
    if (x < -104.0f)
        return 0;
    return (float)exp2_ld((long double)x * 1.44269504088896340735992468100189214L);
}
float ai_powf(float base, float exponent) {
    /* The forward pass calls this only with base=10000 and 0<=exponent<1. */
    if (!(base > 0) || base > 10000 || exponent < 0 || exponent > 1)
        return __builtin_nanf("");
    long double log2base, wide = base;
    __asm__ volatile("fld1; fldt %1; fyl2x; fstpt %0" : "=m"(log2base) : "m"(wide) : "st", "st(1)");
    return (float)exp2_ld(log2base * exponent);
}
float ai_sinf(float x) {
    if (x != x || x > 512 || x < -512)
        return __builtin_nanf("");
    float result;
    __asm__ volatile("flds %1; fsin; fstps %0" : "=m"(result) : "m"(x) : "st");
    return result;
}
float ai_cosf(float x) {
    if (x != x || x > 512 || x < -512)
        return __builtin_nanf("");
    float result;
    __asm__ volatile("flds %1; fcos; fstps %0" : "=m"(result) : "m"(x) : "st");
    return result;
}

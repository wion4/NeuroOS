/* x87 support for the private musl string-to-float adapter. */
long double nos_scale_ld(long double value, int exponent) {
    long double result;
    __asm__ volatile("fildl %2; fldt %1; fscale; fstp %%st(1); fstpt %0"
                     : "=m"(result)
                     : "m"(value), "m"(exponent)
                     : "st", "st(1)");
    return result;
}
long double ldexpl(long double value, int exponent) { return nos_scale_ld(value, exponent); }
long double nos_remainder_ld(long double x, long double y) {
    long double result;
    __asm__ volatile(
        "fldt %2; fldt %1; 1: fprem; fnstsw %%ax; testw $1024,%%ax; jnz 1b; fstp %%st(1); fstpt %0"
        : "=m"(result)
        : "m"(x), "m"(y)
        : "ax", "cc", "st", "st(1)");
    return result;
}

#ifndef NEUROOS_MUSL_CTYPE_H
#define NEUROOS_MUSL_CTYPE_H
static inline int isspace(int c) { return c==' ' || (c>='\t' && c<='\r'); }
#endif

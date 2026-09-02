#ifndef DOT_UTILS_H
#define DOT_UTILS_H 1

#include <stdio.h>

[[nodiscard]]
bool dot_write_quoted(FILE *out, char const *text);

#endif

/* strbuild in C: a growing buffer of 3,000,000 numbers, the same as tests/bench/strbuild.em */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    size_t cap = 16, len = 0;
    char *b = malloc(cap);
    for (long i = 0; i < 3000000; i++) {
        char tmp[24];
        int n = snprintf(tmp, sizeof tmp, "%ld,", i);
        if (len + n > cap) { while (len + n > cap) cap *= 2; b = realloc(b, cap); }
        memcpy(b + len, tmp, n);
        len += n;
    }
    long commas = 0, digits = 0;
    for (size_t i = 0; i < len; i++) { if (b[i] == ',') commas++; else digits += b[i] - '0'; }
    printf("%zu\n%ld\n%ld\n", len, commas, digits);
    return 0;
}

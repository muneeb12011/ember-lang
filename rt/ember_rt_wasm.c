/* ember_rt_wasm.c - the runtime for the WebAssembly target (embc --target wasm), compiled by clang to a freestanding
 * wasm32 module. It shares one linear memory with the compiled program (both import it from the JS loader,
 * rt/ember_wasm.js), which places the program's string literals at address 1024 and this module's data at 16 MB.
 *
 * Differences from the native runtime (rt/ember_rt.c), all forced by the platform:
 *   - no garbage collection: memory is bump-allocated and never reused. WebAssembly locals and the operand stack are
 *     invisible to a conservative stack scanner, so collecting safely would need a shadow stack of roots (future
 *     work). EMBER_HEAP_MB and EMBER_GC_* are ignored.
 *   - everything the outside world provides (output, input, files, arguments, environment, the clock, float
 *     formatting and the math functions) comes from host imports implemented in JavaScript.
 * Every function takes and returns 64-bit values (pointers are zero-extended), matching the code generator.
 */
typedef long long i64;
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned long uptr;

#define IMPORT(name) __attribute__((import_module("host"), import_name(#name)))
#define EXPORT(name) __attribute__((export_name(#name)))

IMPORT(write) void host_write(int fd, const char *p, int n);
IMPORT(read) int host_read(char *p, int max);
IMPORT(exit) __attribute__((noreturn)) void host_exit(int code);
IMPORT(fmt_float) int host_fmt_float(i64 bits, char *out);
IMPORT(fmath) double host_fmath(int fn, double a, double b);
IMPORT(arg_count) int host_arg_count(void);
IMPORT(arg_len) int host_arg_len(int i);
IMPORT(arg_copy) void host_arg_copy(int i, char *out);
IMPORT(env_len) int host_env_len(const char *name, int n);
IMPORT(env_copy) void host_env_copy(const char *name, int n, char *out);
IMPORT(file_read) int host_file_read(const char *path, int n);
IMPORT(file_take) void host_file_take(char *out);
IMPORT(file_write) int host_file_write(const char *path, int n, const char *data, int dn, int append);
IMPORT(file_exists) int host_file_exists(const char *path, int n);
IMPORT(strerror) int host_strerror(int code, char *out);
IMPORT(clock_ns) i64 host_clock_ns(void);

/* the C compiler may call these for struct copies and the like */
void *memcpy(void *d, const void *s, unsigned long n) {
    char *a = (char *)d;
    const char *b = (const char *)s;
    while (n--) *a++ = *b++;
    return d;
}
void *memset(void *d, int c, unsigned long n) {
    char *a = (char *)d;
    while (n--) *a++ = (char)c;
    return d;
}
int memcmp(const void *x, const void *y, unsigned long n) {
    const unsigned char *a = (const unsigned char *)x, *b = (const unsigned char *)y;
    for (; n; n--, a++, b++)
        if (*a != *b) return *a < *b ? -1 : 1;
    return 0;
}

/* ===================================================================================== output */
static char obuf[1 << 16];
static int olen;
static void oflush(void) {
    if (olen) host_write(1, obuf, olen);
    olen = 0;
}
static void oput(const char *s, int n) {
    if (n > (int)sizeof obuf - olen) {
        oflush();
        if (n >= (int)sizeof obuf) {
            host_write(1, s, n);
            return;
        }
    }
    memcpy(obuf + olen, s, (unsigned long)n);
    olen += n;
}
static int utoa(u64 u, char *b) { /* digits of u into b, returns the count */
    char t[24];
    int n = 0;
    do {
        t[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    for (int i = 0; i < n; i++) b[i] = t[n - 1 - i];
    return n;
}
static int itoa(i64 v, char *b) {
    if (v < 0) {
        b[0] = '-';
        return 1 + utoa(0 - (u64)v, b + 1);
    }
    return utoa((u64)v, b);
}

/* ===================================================================================== panics */
static const char *const panic_msgs[] = {"index out of bounds", "division by zero", "negative array size",
                                         "out of memory",       "null dereference", "assertion failed",
                                         "stack overflow"};
static int cstrlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}
static void err(const char *s, int n) { host_write(2, s, n); }
static void errs(const char *s) { err(s, cstrlen(s)); }
static void err_line(i64 line) {
    char b[32];
    errs(" (line ");
    err(b, itoa(line, b));
    errs(")\n");
}

EXPORT(ember_panic) __attribute__((noreturn)) void ember_panic(i64 kind, i64 line) {
    oflush();
    errs("runtime error: ");
    errs((kind >= 0 && kind < 7) ? panic_msgs[kind] : "error");
    err_line(line);
    host_exit(1);
}

/* ===================================================================================== memory */
extern unsigned char __heap_base;
static uptr heap_top, heap_end;

static void *alloc(u64 size) {
    size = (size + 7) & ~(u64)7;
    if (!heap_top) {
        heap_top = ((uptr)&__heap_base + 15) & ~(uptr)15;
        heap_end = (uptr)__builtin_wasm_memory_size(0) * 65536;
    }
    if (size > ((u64)1 << 32) - heap_top) ember_panic(3, 0);
    while (heap_top + size > heap_end) {
        u64 need = heap_top + size - heap_end, pages = (need + 65535) / 65536;
        u64 cur = (u64)__builtin_wasm_memory_size(0);
        if (pages < cur / 2) pages = cur / 2 ? cur / 2 : 1; /* grow geometrically */
        if (__builtin_wasm_memory_grow(0, (unsigned long)pages) == (unsigned long)-1) {
            pages = (need + 65535) / 65536;
            if (__builtin_wasm_memory_grow(0, (unsigned long)pages) == (unsigned long)-1) ember_panic(3, 0);
        }
        heap_end = (uptr)__builtin_wasm_memory_size(0) * 65536;
    }
    void *p = (void *)heap_top;
    heap_top += size;
    memset(p, 0, (unsigned long)size); /* fresh pages are zero, but be explicit */
    return p;
}

#define P(x) ((char *)(uptr)(x))
#define SLEN(s) (*(i64 *)P(s))
#define SDATA(s) (P(s) + 8)

EXPORT(ember_new_array) i64 ember_new_array(i64 n, i64 w, i64 noscan, i64 line) {
    (void)noscan;
    if (n < 0) ember_panic(2, line);
    if ((u64)n > ((u64)1 << 31)) ember_panic(3, line);
    u64 size = 8 + (u64)n * (u64)w;
    if (size > ((u64)1 << 32)) ember_panic(3, line);
    i64 *p = (i64 *)alloc(size);
    *p = n;
    return (i64)(uptr)p;
}

static i64 new_str(u64 n) {
    i64 *p = (i64 *)alloc(8 + n);
    *p = (i64)n;
    return (i64)(uptr)p;
}

static i64 empty_str_obj[1] = {0};

EXPORT(ember_new_str_array) i64 ember_new_str_array(i64 n, i64 line) {
    i64 a = ember_new_array(n, 8, 0, line);
    i64 *e = (i64 *)P(a);
    for (i64 i = 0; i < n; i++) e[1 + i] = (i64)(uptr)empty_str_obj;
    return a;
}

/* ===================================================================================== strings */
EXPORT(ember_str_concat) i64 ember_str_concat(i64 a, i64 b) {
    i64 la = SLEN(a), lb = SLEN(b);
    i64 r = new_str((u64)(la + lb));
    memcpy(SDATA(r), SDATA(a), (unsigned long)la);
    memcpy(SDATA(r) + la, SDATA(b), (unsigned long)lb);
    return r;
}
EXPORT(ember_str_cmp) i64 ember_str_cmp(i64 a, i64 b) {
    i64 la = SLEN(a), lb = SLEN(b), n = la < lb ? la : lb;
    int c = memcmp(SDATA(a), SDATA(b), (unsigned long)n);
    if (c) return c < 0 ? -1 : 1;
    return la < lb ? -1 : la > lb ? 1 : 0;
}
EXPORT(ember_substr) i64 ember_substr(i64 s, i64 start, i64 count, i64 line) {
    i64 len = SLEN(s);
    if (start < 0 || count < 0 || start > len || count > len - start) ember_panic(0, line);
    i64 r = new_str((u64)count);
    memcpy(SDATA(r), SDATA(s) + start, (unsigned long)count);
    return r;
}
EXPORT(ember_chr) i64 ember_chr(i64 c) {
    i64 r = new_str(1);
    SDATA(r)[0] = (char)c;
    return r;
}
EXPORT(ember_str_from_int) i64 ember_str_from_int(i64 v) {
    char b[32];
    int n = itoa(v, b);
    i64 r = new_str((u64)n);
    memcpy(SDATA(r), b, (unsigned long)n);
    return r;
}
EXPORT(ember_str_from_float) i64 ember_str_from_float(i64 bits) {
    char b[400];
    int n = host_fmt_float(bits, b);
    i64 r = new_str((u64)n);
    memcpy(SDATA(r), b, (unsigned long)n);
    return r;
}
EXPORT(ember_str_from_bool) i64 ember_str_from_bool(i64 v) {
    int n = v ? 4 : 5;
    i64 r = new_str((u64)n);
    memcpy(SDATA(r), v ? "true" : "false", (unsigned long)n);
    return r;
}
EXPORT(ember_str_from_bytes) i64 ember_str_from_bytes(i64 arr, i64 n, i64 line) {
    if (!arr) ember_panic(4, line);
    if (n < 0 || n > *(i64 *)P(arr)) ember_panic(0, line);
    i64 r = new_str((u64)n);
    memcpy(SDATA(r), P(arr) + 8, (unsigned long)n);
    return r;
}
EXPORT(ember_parse_int) i64 ember_parse_int(i64 s) {
    const char *p = SDATA(s), *e = p + SLEN(s);
    while (p < e && (*p == ' ' || (*p >= 9 && *p <= 13))) p++;
    int neg = 0;
    if (p < e && (*p == '-' || *p == '+')) neg = *p++ == '-';
    u64 v = 0;
    while (p < e && *p >= '0' && *p <= '9') v = v * 10 + (u64)(*p++ - '0');
    return (i64)(neg ? 0 - v : v);
}

/* ===================================================================================== printing */
EXPORT(ember_print_int) void ember_print_int(i64 v) {
    char b[32];
    int n = itoa(v, b);
    b[n++] = '\n';
    oput(b, n);
}
EXPORT(ember_print_float) void ember_print_float(i64 bits) {
    char b[400];
    int n = host_fmt_float(bits, b);
    b[n++] = '\n';
    oput(b, n);
}
EXPORT(ember_print_bool) void ember_print_bool(i64 v) {
    if (v) oput("true\n", 5);
    else oput("false\n", 6);
}
EXPORT(ember_print_str) void ember_print_str(i64 s) {
    oput(SDATA(s), (int)SLEN(s));
    oput("\n", 1);
}
EXPORT(ember_put_str) void ember_put_str(i64 s) { oput(SDATA(s), (int)SLEN(s)); }

/* ===================================================================================== input */
static char ibuf[1 << 16];
static int ipos, ilen, ieof;
static int in_peek(void) {
    if (ipos < ilen) return (unsigned char)ibuf[ipos];
    if (ieof) return -1;
    oflush();
    int n = host_read(ibuf, (int)sizeof ibuf);
    if (n <= 0) {
        ieof = 1;
        return -1;
    }
    ipos = 0;
    ilen = n;
    return (unsigned char)ibuf[0];
}
EXPORT(ember_read_int) i64 ember_read_int(void) {
    int c;
    while ((c = in_peek()) == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') ipos++;
    int neg = 0;
    if (c == '-' || c == '+') {
        neg = c == '-';
        ipos++;
    }
    u64 v = 0;
    while ((c = in_peek()) >= '0' && c <= '9') {
        v = v * 10 + (u64)(c - '0');
        ipos++;
    }
    return (i64)(neg ? 0 - v : v);
}
EXPORT(ember_read_line) i64 ember_read_line(void) {
    u64 cap = 128, n = 0;
    char *buf = (char *)alloc(cap);
    int c;
    while ((c = in_peek()) >= 0) {
        ipos++;
        if (c == '\n') break;
        if (n == cap) {
            char *nb = (char *)alloc(cap * 2);
            memcpy(nb, buf, (unsigned long)n);
            buf = nb;
            cap *= 2;
        }
        buf[n++] = (char)c;
    }
    i64 r = new_str(n);
    memcpy(SDATA(r), buf, (unsigned long)n);
    return r;
}
EXPORT(ember_at_eof) i64 ember_at_eof(void) { return in_peek() < 0; }

/* ===================================================================================== misc */
EXPORT(ember_arg_count) i64 ember_arg_count(void) { return host_arg_count(); }
EXPORT(ember_arg) i64 ember_arg(i64 i, i64 line) {
    if (i < 0 || i >= host_arg_count()) ember_panic(0, line);
    int n = host_arg_len((int)i);
    i64 r = new_str((u64)n);
    host_arg_copy((int)i, SDATA(r));
    return r;
}
EXPORT(ember_panic_str) __attribute__((noreturn)) void ember_panic_str(i64 s, i64 line) {
    oflush();
    errs("runtime error: panic: ");
    err(SDATA(s), (int)SLEN(s));
    err_line(line);
    host_exit(1);
}
static double dbl(i64 b) {
    union { i64 i; double d; } u;
    u.i = b;
    return u.d;
}
static i64 bits(double d) {
    union { i64 i; double d; } u;
    u.d = d;
    return u.i;
}
static int is_nan(i64 b) { return ((b >> 52) & 0x7ff) == 0x7ff && (b & 0xFFFFFFFFFFFFFLL) != 0; }
/* JavaScript's Math functions return a positive NaN; glibc (and so the x86-64 target) propagates an input NaN and
 * otherwise returns the processor's default NaN, which has the sign bit set and prints as "-nan" */
EXPORT(ember_fmath) i64 ember_fmath(i64 fn, i64 a, i64 b) {
    i64 r = bits(host_fmath((int)fn, dbl(a), dbl(b)));
    if (!is_nan(r)) return r;
    if (is_nan(a)) return a | 0x0008000000000000LL;
    if ((fn == 4 || fn == 7) && is_nan(b)) return b | 0x0008000000000000LL;
    return (i64)0xFFF8000000000000ULL;
}

/* ===================================================================================== files, process */
__attribute__((noreturn)) static void io_error(const char *what, i64 path, int code, i64 line) {
    oflush();
    char m[200];
    int n = host_strerror(code, m);
    errs("runtime error: cannot ");
    errs(what);
    errs(" `");
    err(SDATA(path), (int)SLEN(path));
    errs("`: ");
    err(m, n);
    err_line(line);
    host_exit(1);
}
EXPORT(ember_read_file) i64 ember_read_file(i64 path, i64 line) {
    int n = host_file_read(SDATA(path), (int)SLEN(path));
    if (n < 0) io_error("read", path, -n, line);
    i64 r = new_str((u64)n);
    host_file_take(SDATA(r));
    return r;
}
static void put_file(i64 path, i64 data, int append, i64 line) {
    int rc = host_file_write(SDATA(path), (int)SLEN(path), SDATA(data), (int)SLEN(data), append);
    if (rc < 0) io_error("write", path, -rc, line);
}
EXPORT(ember_write_file) void ember_write_file(i64 path, i64 data, i64 line) {
    oflush();
    put_file(path, data, 0, line);
}
EXPORT(ember_append_file) void ember_append_file(i64 path, i64 data, i64 line) {
    oflush();
    put_file(path, data, 1, line);
}
EXPORT(ember_file_exists) i64 ember_file_exists(i64 path) { return host_file_exists(SDATA(path), (int)SLEN(path)); }
EXPORT(ember_env) i64 ember_env(i64 name) {
    int n = host_env_len(SDATA(name), (int)SLEN(name));
    if (n < 0) n = 0;
    i64 r = new_str((u64)n);
    if (n) host_env_copy(SDATA(name), (int)SLEN(name), SDATA(r));
    return r;
}
EXPORT(ember_has_env) i64 ember_has_env(i64 name) { return host_env_len(SDATA(name), (int)SLEN(name)) >= 0; }
EXPORT(ember_exit) __attribute__((noreturn)) void ember_exit(i64 code) {
    oflush();
    host_exit((int)(code & 255));
}
EXPORT(ember_clock_ns) i64 ember_clock_ns(void) { return host_clock_ns(); }
EXPORT(ember_flush) void ember_flush(void) { oflush(); }

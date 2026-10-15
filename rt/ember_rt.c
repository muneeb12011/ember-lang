/* ember_rt.c - runtime library linked into every compiled Ember program.
 *
 *   1. Memory: a conservative, non-moving mark-sweep garbage collector.
 *        small objects (<= 2 KB)  size-class blocks of 64 KB inside one reserved address range, with
 *                                 per-block alloc/mark bitmaps and free lists threaded through free slots
 *        large objects            calloc'd individually, kept in an address-sorted table
 *        roots                    the Ember thread's stack + callee-saved registers (conservative)
 *        pointer-free objects     arrays of int/float/bool/byte and strings are never scanned
 *      Interior pointers are accepted when marking, so C code that keeps only `base + 8` stays safe.
 *   2. Buffered stdout / stdin, string helpers, runtime panics, math wrappers.
 *   3. Ember runs on a dedicated thread with a large lazily-committed stack; stack overflow is detected
 *      with a SIGSEGV handler on an alternate stack and reported as a normal runtime error.
 *
 * Debug knobs (environment): EMBER_GC_STRESS=N collects on every Nth allocation (1 = every one),
 * EMBER_GC_STATS=1 prints collector statistics to stderr at exit.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

typedef long long i64;
typedef unsigned long long u64;

extern i64 ember_main(void);

/* ===================================================================================== output */
static char obuf[1 << 16];
static size_t olen;

static void write_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        p += w;
        n -= (size_t)w;
    }
}
static void oflush(void) {
    if (olen) {
        write_all(1, obuf, olen);
        olen = 0;
    }
}
static void oput(const char *s, size_t n) {
    if (n > sizeof obuf - olen) {
        oflush();
        if (n >= sizeof obuf) {
            write_all(1, s, n);
            return;
        }
    }
    memcpy(obuf + olen, s, n);
    olen += n;
}

/* ===================================================================================== panics */
static const char *const panic_msgs[] = {"index out of bounds", "division by zero", "negative array size",
                                         "out of memory",       "null dereference", "assertion failed",
                                         "stack overflow"};

static size_t heap_limit; /* bytes, from EMBER_HEAP_MB; 0 = no limit */

__attribute__((noreturn)) void ember_panic(i64 kind, i64 line) {
    oflush();
    char b[200];
    const char *m = (kind >= 0 && kind < 7) ? panic_msgs[kind] : "error";
    int n;
    if (kind == 3 && heap_limit)
        n = snprintf(b, sizeof b, "runtime error: out of memory: the heap limit EMBER_HEAP_MB=%zu was reached (line %lld)\n",
                     heap_limit >> 20, line);
    else
        n = snprintf(b, sizeof b, "runtime error: %s (line %lld)\n", m, line);
    write_all(2, b, (size_t)n);
    _exit(1);
}

/* ===================================================================================== heap */
/* Generational, non-moving, conservative mark-sweep ("sticky mark bits").
 *
 *   Why non-moving: the stack and registers are scanned conservatively (any word that looks like a pointer keeps its
 *   target alive), so an object cannot be moved: the "pointer" might be an integer that must not be rewritten.
 *   Why generational rather than incremental: most Ember allocations are short-lived temporaries (string pieces,
 *   enum values, closures, small structs). A minor collection only visits young objects reachable from the roots and
 *   from old objects that were written to, so it costs about what survives, not the size of the heap; that cuts both
 *   total collection time and pause length. Incremental marking would only spread the same total work out, and needs
 *   a write barrier on every pointer store anyway.
 *
 *   An object is old once it has survived one collection: its mark bit is simply kept set ("sticky") instead of being
 *   cleared. A minor collection marks from the roots, from the dirty cards and from the old large objects that hold
 *   pointers, visits only unmarked (young) objects, and frees the young objects it did not reach. A major collection
 *   first clears every mark bit, so old objects can die too. A major collection happens when the old generation has
 *   doubled since the last one (and at least every 16th stress collection).
 *
 *   Write barrier: compiled code that stores a pointer into an existing heap object marks the 512-byte card holding
 *   that object as dirty (ember_heap_base / ember_heap_span / ember_cards; see codegen.hpp). Large objects live
 *   outside the block range and are not covered by cards; the old ones that hold pointers are rescanned at every minor
 *   collection instead.
 *
 *   small objects (<= 2 KB)  size-class blocks of 64 KB inside one reserved address range, with per-block alloc/mark
 *                            bitmaps and free lists threaded through free slots
 *   large objects            calloc'd individually, kept in an address-sorted table
 *   roots                    the Ember thread's stack + callee-saved registers (conservative)
 *   pointer-free objects     arrays of int/float/bool/byte and strings are never scanned
 *   Interior pointers are accepted when marking, so C code that keeps only `base + 8` stays safe. Marking uses an
 *   explicit stack, so deep structures (a list of millions of nodes) cannot overflow the C stack.
 *
 * Knobs: EMBER_HEAP_MB=N fails with "out of memory" instead of growing the heap past N MB; EMBER_GC_STRESS=N collects
 * on every Nth allocation; EMBER_GC_STATS=1 prints collector statistics (including pause times) at exit;
 * EMBER_GC_FULL=1 makes every collection a full one (the old, non-generational policy, kept for comparison).
 */
#define BLK_SHIFT 16
#define BLK_SIZE ((uintptr_t)1 << BLK_SHIFT)
#define MAX_SMALL 2048
#define MAX_OBJ (BLK_SIZE / 16)
#define NCLS 24
#define CARD_SHIFT 9
#define CARDS_PER_BLOCK (BLK_SIZE >> CARD_SHIFT)
#define NURSERY ((size_t)4 << 20)     /* a minor collection after this many bytes of new objects */
#define MIN_MAJOR ((size_t)8 << 20)   /* the old generation may grow to at least this before a major collection */

static const unsigned short cls_size[NCLS] = {16,  32,  48,  64,  80,  96,   112,  128,  160,  192,  224,  256,
                                              320, 384, 448, 512, 640, 768,  896,  1024, 1280, 1536, 1792, 2048};
static unsigned char cls_of[MAX_SMALL / 16 + 1];

typedef struct Block {
    uint32_t objsize; /* 0 = block is free */
    uint32_t nobj;
    uint32_t recip; /* floor(2^32 / objsize) + 1: exact index = (offset * recip) >> 32 for offsets < 64K */
    uint8_t cls, noscan;
    uint64_t alloc[MAX_OBJ / 64];
    uint64_t mark[MAX_OBJ / 64]; /* sticky: set = old (or reached in this collection) */
} Block;

typedef struct Large {
    uintptr_t p;
    size_t size;
    uint8_t mark, noscan;
} Large;

/* read by the write barrier in compiled code */
uintptr_t ember_heap_base;
uintptr_t ember_heap_span; /* 0 until the heap exists, so the barrier's range test fails */
uint8_t *ember_cards;

static int heap_ready;
static uintptr_t heap_base;
static size_t max_blocks, blocks_used;
static Block *meta;
static uint32_t *free_blocks;
static size_t nfree_blocks, cap_free_blocks;
static void *freelist[2][NCLS];
static Large *larges;
static size_t nlarges, cap_larges;
static uintptr_t *mstack;
static size_t msp, mcap;

static size_t since_gc, old_bytes, major_at = MIN_MAJOR, peak_live, large_bytes;
static size_t n_minor, n_major, alloc_counter;
static double pause_total, pause_max, pause_minor_total, pause_major_total;
static long gc_stress;
static int gc_stats, gc_full;
static uintptr_t stack_base, stack_lo;

static void fatal_oom(void) { ember_panic(3, 0); }

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void heap_init(void) {
    for (int k = 0, c = 0; k <= MAX_SMALL / 16; k++) {
        while (cls_size[c] < (size_t)k * 16) c++;
        cls_of[k] = (unsigned char)c;
    }
    size_t want = (size_t)16 << 30;
    void *p = MAP_FAILED;
    for (; want >= ((size_t)256 << 20); want >>= 1) {
        p = mmap(NULL, want + BLK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (p != MAP_FAILED) break;
    }
    if (p == MAP_FAILED) fatal_oom();
    heap_base = ((uintptr_t)p + BLK_SIZE - 1) & ~(BLK_SIZE - 1);
    max_blocks = want >> BLK_SHIFT;
    void *m = mmap(NULL, max_blocks * sizeof(Block), PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (m == MAP_FAILED) fatal_oom();
    meta = (Block *)m;
    void *c = mmap(NULL, max_blocks * CARDS_PER_BLOCK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (c == MAP_FAILED) fatal_oom();
    ember_cards = (uint8_t *)c;
    ember_heap_base = heap_base;
    ember_heap_span = max_blocks << BLK_SHIFT;
    heap_ready = 1;
}

/* ---- large-object table (sorted by address) ---- */
static Large *find_large(uintptr_t a) {
    size_t lo = 0, hi = nlarges;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (larges[mid].p <= a) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return NULL;
    Large *L = &larges[lo - 1];
    return a < L->p + L->size ? L : NULL;
}
static void add_large(uintptr_t p, size_t size, int noscan) {
    if (nlarges == cap_larges) {
        cap_larges = cap_larges ? cap_larges * 2 : 64;
        larges = (Large *)realloc(larges, cap_larges * sizeof(Large));
        if (!larges) fatal_oom();
    }
    size_t lo = 0, hi = nlarges;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (larges[mid].p < p) lo = mid + 1;
        else hi = mid;
    }
    memmove(&larges[lo + 1], &larges[lo], (nlarges - lo) * sizeof(Large));
    larges[lo].p = p;
    larges[lo].size = size;
    larges[lo].mark = 0;
    larges[lo].noscan = (uint8_t)noscan;
    nlarges++;
}

/* ---- marking ---- */
static void mpush(uintptr_t p) {
    if (msp == mcap) {
        mcap = mcap ? mcap * 2 : 4096;
        mstack = (uintptr_t *)realloc(mstack, mcap * sizeof(uintptr_t));
        if (!mstack) fatal_oom();
    }
    mstack[msp++] = p;
}

/* a word that may point into an unmarked (young, or anything in a major collection) object: mark it, queue it */
static inline void consider(uintptr_t w) {
    uintptr_t off = w - heap_base;
    if (off < (blocks_used << BLK_SHIFT)) {
        Block *b = &meta[off >> BLK_SHIFT];
        if (!b->objsize) return;
        uint32_t in = (uint32_t)(off & (BLK_SIZE - 1));
        uint32_t idx = (uint32_t)(((u64)in * b->recip) >> 32);
        if (idx >= b->nobj) return;
        uint64_t bit = 1ull << (idx & 63);
        if (!(b->alloc[idx >> 6] & bit) || (b->mark[idx >> 6] & bit)) return;
        b->mark[idx >> 6] |= bit;
        if (!b->noscan) mpush(heap_base + (off & ~(BLK_SIZE - 1)) + (uintptr_t)idx * b->objsize);
    } else if (nlarges) {
        Large *L = find_large(w);
        if (L && !L->mark) {
            L->mark = 1;
            if (!L->noscan) mpush(L->p);
        }
    }
}

static void scan_words(uintptr_t p, size_t words) {
    uintptr_t *q = (uintptr_t *)p;
    for (size_t i = 0; i < words; i++) consider(q[i]);
}

static void drain(void) {
    while (msp) {
        uintptr_t p = mstack[--msp];
        uintptr_t off = p - heap_base;
        size_t words;
        if (off < (blocks_used << BLK_SHIFT)) {
            words = meta[off >> BLK_SHIFT].objsize / 8;
        } else {
            Large *L = find_large(p);
            words = L ? L->size / 8 : 0;
        }
        scan_words(p, words);
    }
}

/* minor collection: old objects in dirty cards may point to young ones */
static void scan_dirty_cards(void) {
    for (size_t bi = 0; bi < blocks_used; bi++) {
        Block *b = &meta[bi];
        uint8_t *cards = ember_cards + bi * CARDS_PER_BLOCK;
        if (!b->objsize || b->noscan) continue;
        uintptr_t base = heap_base + (bi << BLK_SHIFT);
        for (uint32_t c = 0; c < CARDS_PER_BLOCK; c++) {
            if (!cards[c]) continue;
            uint32_t lo = (uint32_t)((c << CARD_SHIFT) / b->objsize);
            uint32_t hi = (uint32_t)((((c + 1) << CARD_SHIFT) - 1) / b->objsize);
            for (uint32_t i = lo; i <= hi && i < b->nobj; i++) {
                uint64_t bit = 1ull << (i & 63);
                if ((b->alloc[i >> 6] & bit) && (b->mark[i >> 6] & bit)) scan_words(base + (uintptr_t)i * b->objsize, b->objsize / 8);
            }
        }
    }
    for (size_t i = 0; i < nlarges; i++) /* old large objects that hold pointers */
        if (larges[i].mark && !larges[i].noscan) scan_words(larges[i].p, larges[i].size / 8);
}

static void release_block(size_t bi) {
    Block *b = &meta[bi];
    b->objsize = 0;
    if (nfree_blocks == cap_free_blocks) {
        cap_free_blocks = cap_free_blocks ? cap_free_blocks * 2 : 256;
        free_blocks = (uint32_t *)realloc(free_blocks, cap_free_blocks * sizeof(uint32_t));
        if (!free_blocks) fatal_oom();
    }
    free_blocks[nfree_blocks++] = (uint32_t)bi;
    madvise((void *)(heap_base + (bi << BLK_SHIFT)), BLK_SIZE, MADV_DONTNEED); /* give the pages back */
}

/* free every allocated object that is not marked; the marked ones (old ones too) stay marked, i.e. old */
static void sweep(void) {
    memset(freelist, 0, sizeof freelist);
    size_t live = 0;
    for (size_t bi = 0; bi < blocks_used; bi++) {
        Block *b = &meta[bi];
        if (!b->objsize) continue;
        uint32_t nlive = 0;
        size_t words = (b->nobj + 63) / 64;
        for (size_t w = 0; w < words; w++) nlive += (uint32_t)__builtin_popcountll(b->mark[w]);
        if (nlive == 0) {
            release_block(bi);
            continue;
        }
        live += (size_t)nlive * b->objsize;
        uintptr_t base = heap_base + (bi << BLK_SHIFT);
        void **fl = &freelist[b->noscan][b->cls];
        for (uint32_t i = b->nobj; i-- > 0;) {
            if (!(b->mark[i >> 6] & (1ull << (i & 63)))) {
                void *p = (void *)(base + (uintptr_t)i * b->objsize);
                *(void **)p = *fl;
                *fl = p;
            }
        }
        for (size_t w = 0; w < words; w++) b->alloc[w] = b->mark[w];
    }
    size_t out = 0;
    large_bytes = 0;
    for (size_t i = 0; i < nlarges; i++) {
        if (larges[i].mark) {
            large_bytes += larges[i].size;
            larges[out++] = larges[i];
        } else {
            free((void *)larges[i].p);
        }
    }
    nlarges = out;
    old_bytes = live + large_bytes;
    if (old_bytes > peak_live) peak_live = old_bytes;
    memset(ember_cards, 0, blocks_used * CARDS_PER_BLOCK);
}

static void clear_marks(void) {
    for (size_t bi = 0; bi < blocks_used; bi++)
        if (meta[bi].objsize) memset(meta[bi].mark, 0, sizeof meta[bi].mark);
    for (size_t i = 0; i < nlarges; i++) larges[i].mark = 0;
}

__attribute__((noinline)) static void collect_from(uintptr_t sp, int major) {
    double t0 = now_sec();
    if (major) clear_marks();
    else scan_dirty_cards();
    for (uintptr_t *p = (uintptr_t *)(sp & ~(uintptr_t)7); (uintptr_t)p < stack_base; p++) consider(*p);
    drain();
    sweep();
    since_gc = 0;
    if (major) {
        n_major++;
        major_at = old_bytes * 2 > MIN_MAJOR ? old_bytes * 2 : MIN_MAJOR;
    } else {
        n_minor++;
    }
    double dt = now_sec() - t0;
    pause_total += dt;
    if (major) pause_major_total += dt;
    else pause_minor_total += dt;
    if (dt > pause_max) pause_max = dt;
}

__attribute__((noinline)) static void collect(int major) {
    if (!heap_ready) return;
    uintptr_t regs[8];
    __asm__ volatile(
        "movq %%rbx, 0(%0)\n\t"
        "movq %%r12, 8(%0)\n\t"
        "movq %%r13, 16(%0)\n\t"
        "movq %%r14, 24(%0)\n\t"
        "movq %%r15, 32(%0)\n\t"
        "movq %%rbp, 40(%0)\n\t"
        :
        : "r"(regs)
        : "memory");
    collect_from((uintptr_t)regs, major);
}

static size_t heap_bytes(void) { return ((blocks_used - nfree_blocks) << BLK_SHIFT) + large_bytes; }

/* ---- allocation ---- */
static Block *new_block(int cls, int noscan) {
    size_t bi;
    if (heap_limit && heap_bytes() + BLK_SIZE > heap_limit) return NULL;
    if (nfree_blocks) bi = free_blocks[--nfree_blocks];
    else if (blocks_used < max_blocks) bi = blocks_used++;
    else return NULL;
    Block *b = &meta[bi];
    uint32_t sz = cls_size[cls];
    b->objsize = sz;
    b->nobj = (uint32_t)(BLK_SIZE / sz);
    b->recip = (uint32_t)((((u64)1) << 32) / sz + 1);
    b->cls = (uint8_t)cls;
    b->noscan = (uint8_t)noscan;
    memset(b->alloc, 0, sizeof b->alloc);
    memset(b->mark, 0, sizeof b->mark);
    memset(ember_cards + bi * CARDS_PER_BLOCK, 0, CARDS_PER_BLOCK);
    uintptr_t base = heap_base + (bi << BLK_SHIFT);
    void **fl = &freelist[noscan][cls];
    for (uint32_t i = b->nobj; i-- > 0;) {
        void *p = (void *)(base + (uintptr_t)i * sz);
        *(void **)p = *fl;
        *fl = p;
    }
    return b;
}

static void *alloc_small(size_t size, int noscan, i64 line) {
    int c = cls_of[(size + 15) >> 4];
    void *p = freelist[noscan][c];
    if (__builtin_expect(!p, 0)) {
        if (!new_block(c, noscan)) {
            collect(1);
            if (!freelist[noscan][c] && !new_block(c, noscan)) ember_panic(3, line);
        }
        p = freelist[noscan][c];
    }
    freelist[noscan][c] = *(void **)p;
    uintptr_t off = (uintptr_t)p - heap_base;
    Block *b = &meta[off >> BLK_SHIFT];
    uint32_t idx = (uint32_t)(((u64)(off & (BLK_SIZE - 1)) * b->recip) >> 32);
    b->alloc[idx >> 6] |= 1ull << (idx & 63);
    memset(p, 0, cls_size[c]);
    return p;
}

static void *alloc_large(size_t size, int noscan, i64 line) {
    if (heap_limit && heap_bytes() + size > heap_limit) {
        collect(1);
        if (heap_bytes() + size > heap_limit) ember_panic(3, line);
    }
    void *p = calloc(1, size);
    if (!p) {
        collect(1);
        p = calloc(1, size);
        if (!p) ember_panic(3, line);
    }
    add_large((uintptr_t)p, size, noscan);
    large_bytes += size;
    return p;
}

static void *gc_alloc(size_t size, int noscan, i64 line) {
    if (__builtin_expect(!heap_ready, 0)) heap_init();
    size = (size + 7) & ~(size_t)7;
    since_gc += size;
    if (__builtin_expect(gc_stress > 0, 0) && ++alloc_counter % (size_t)gc_stress == 0) {
        collect((alloc_counter / (size_t)gc_stress) % 16 == 0); /* mostly minor: exercises the write barrier */
    } else if (__builtin_expect(since_gc >= NURSERY, 0)) {
        if (gc_full) {  /* the non-generational policy, for comparison: full collections once the heap doubles */
            if (since_gc >= old_bytes) collect(1);
        } else {
            collect(old_bytes >= major_at);
        }
    }
    return size <= MAX_SMALL ? alloc_small(size, noscan, line) : alloc_large(size, noscan, line);
}

/* array / struct block: [i64 length][elements...] */
void *ember_new_array(i64 n, i64 w, i64 noscan, i64 line) {
    if (n < 0) ember_panic(2, line);
    if ((u64)n > ((u64)1 << 31)) ember_panic(3, line); /* same limit as the reference interpreter */
    size_t size = 8 + (size_t)n * (size_t)w;
    void *p = gc_alloc(size, (int)noscan, line);
    *(i64 *)p = n;
    return p;
}

void ember_gc(void) { collect(1); }

/* ===================================================================================== strings */
/* a str is [i64 length][bytes...], never null, immutable */
static void *new_str(size_t n) {
    void *p = gc_alloc(8 + n, 1, 0);
    *(i64 *)p = (i64)n;
    return p;
}
#define SLEN(s) (*(i64 *)(s))
#define SDATA(s) ((char *)(s) + 8)
#define KEEP(p) __asm__ volatile("" : : "r"(p) : "memory")

void *ember_str_concat(void *a, void *b) {
    size_t la = (size_t)SLEN(a), lb = (size_t)SLEN(b);
    void *r = new_str(la + lb);
    memcpy(SDATA(r), SDATA(a), la);
    memcpy(SDATA(r) + la, SDATA(b), lb);
    KEEP(a);
    KEEP(b);
    return r;
}

i64 ember_str_cmp(void *a, void *b) {
    size_t la = (size_t)SLEN(a), lb = (size_t)SLEN(b), n = la < lb ? la : lb;
    int c = memcmp(SDATA(a), SDATA(b), n);
    if (c) return c < 0 ? -1 : 1;
    return la < lb ? -1 : la > lb ? 1 : 0;
}

void *ember_substr(void *s, i64 start, i64 count, i64 line) {
    i64 len = SLEN(s);
    if (start < 0 || count < 0 || start > len || count > len - start) ember_panic(0, line);
    void *r = new_str((size_t)count);
    memcpy(SDATA(r), SDATA(s) + start, (size_t)count);
    KEEP(s);
    return r;
}

void *ember_chr(i64 c) {
    void *r = new_str(1);
    SDATA(r)[0] = (char)c;
    return r;
}

void *ember_str_from_int(i64 v) {
    char b[32];
    int n = snprintf(b, sizeof b, "%lld", v);
    void *r = new_str((size_t)n);
    memcpy(SDATA(r), b, (size_t)n);
    return r;
}

static i64 empty_str_obj[1] = {0}; /* the shared "": length 0, lives outside the heap */

/* new [str](n): every element starts as "" (a str is never null) */
void *ember_new_str_array(i64 n, i64 line) {
    i64 *p = (i64 *)ember_new_array(n, 8, 0, line);
    for (i64 i = 0; i < n; i++) p[1 + i] = (i64)empty_str_obj;
    return p;
}

/* the first n bytes of a [byte] as a new str */
void *ember_str_from_bytes(void *arr, i64 n, i64 line) {
    if (!arr) ember_panic(4, line);
    if (n < 0 || n > *(i64 *)arr) ember_panic(0, line);
    void *r = new_str((size_t)n);
    memcpy(SDATA(r), (char *)arr + 8, (size_t)n);
    KEEP(arr);
    return r;
}

static double bits_to_double(i64 bits) {
    double d;
    memcpy(&d, &bits, 8);
    return d;
}
static i64 double_to_bits(double d) {
    i64 b;
    memcpy(&b, &d, 8);
    return b;
}

void *ember_str_from_float(i64 bits) {
    char b[400];
    int n = snprintf(b, sizeof b, "%.6f", bits_to_double(bits));
    void *r = new_str((size_t)n);
    memcpy(SDATA(r), b, (size_t)n);
    return r;
}

void *ember_str_from_bool(i64 v) {
    size_t n = v ? 4 : 5;
    void *r = new_str(n);
    memcpy(SDATA(r), v ? "true" : "false", n);
    return r;
}

i64 ember_parse_int(void *s) {
    const char *p = SDATA(s), *e = p + SLEN(s);
    while (p < e && (*p == ' ' || (*p >= 9 && *p <= 13))) p++; /* leading whitespace, like read_int */
    int neg = 0;
    if (p < e && (*p == '-' || *p == '+')) neg = *p++ == '-';
    u64 v = 0;
    while (p < e && *p >= '0' && *p <= '9') v = v * 10 + (u64)(*p++ - '0');
    return (i64)(neg ? 0 - v : v);
}

/* ===================================================================================== printing */
void ember_print_int(i64 v) {
    char b[32];
    char *e = b + 31;
    *e = '\n';
    u64 u = v < 0 ? 0 - (u64)v : (u64)v;
    char *p = e;
    do {
        *--p = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0) *--p = '-';
    oput(p, (size_t)(b + 32 - p));
}
void ember_print_float(i64 bits) {
    char b[400];
    int n = snprintf(b, sizeof b, "%.6f\n", bits_to_double(bits));
    oput(b, (size_t)n);
}
void ember_print_bool(i64 v) {
    if (v) oput("true\n", 5);
    else oput("false\n", 6);
}
void ember_print_str(void *s) {
    oput(SDATA(s), (size_t)SLEN(s));
    oput("\n", 1);
}
void ember_put_str(void *s) { oput(SDATA(s), (size_t)SLEN(s)); }

/* ===================================================================================== input */
static unsigned char ibuf[1 << 16];
static size_t ipos, ilen;
static int ieof;

static int in_peek(void) {
    if (ipos < ilen) return ibuf[ipos];
    if (ieof) return -1;
    oflush(); /* so an interactive prompt appears before we block */
    ssize_t n;
    do n = read(0, ibuf, sizeof ibuf);
    while (n < 0 && errno == EINTR);
    if (n <= 0) {
        ieof = 1;
        return -1;
    }
    ipos = 0;
    ilen = (size_t)n;
    return ibuf[0];
}
static int in_get(void) {
    int c = in_peek();
    if (c >= 0) ipos++;
    return c;
}

i64 ember_read_int(void) {
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

void *ember_read_line(void) {
    size_t cap = 128, n = 0;
    char *buf = (char *)malloc(cap);
    int c;
    while ((c = in_get()) >= 0 && c != '\n') {
        if (n == cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
        buf[n++] = (char)c;
    }
    void *r = new_str(n);
    memcpy(SDATA(r), buf, n);
    free(buf);
    return r;
}

i64 ember_at_eof(void) { return in_peek() < 0; }

/* ===================================================================================== misc */
static int g_argc;
static char **g_argv;

i64 ember_arg_count(void) { return g_argc - 1; }
void *ember_arg(i64 i, i64 line) {
    if (i < 0 || i >= g_argc - 1) ember_panic(0, line);
    const char *a = g_argv[i + 1];
    size_t n = strlen(a);
    void *r = new_str(n);
    memcpy(SDATA(r), a, n);
    return r;
}

__attribute__((noreturn)) void ember_panic_str(void *s, i64 line) {
    oflush();
    char b[64];
    int n = snprintf(b, sizeof b, " (line %lld)\n", line);
    write_all(2, "runtime error: panic: ", 22);
    write_all(2, SDATA(s), (size_t)SLEN(s));
    write_all(2, b, (size_t)n);
    _exit(1);
}

/* ===================================================================================== files, process */
static void cstr(void *s, char *buf, size_t cap) { /* a str as a NUL-terminated C string (truncated to cap) */
    size_t n = (size_t)SLEN(s);
    if (n >= cap) n = cap - 1;
    memcpy(buf, SDATA(s), n);
    buf[n] = 0;
}

__attribute__((noreturn)) static void io_error(const char *what, void *path, i64 line) {
    int e = errno;
    oflush();
    char p[4096], b[4400];
    cstr(path, p, sizeof p);
    int n = snprintf(b, sizeof b, "runtime error: cannot %s `%s`: %s (line %lld)\n", what, p, strerror(e), line);
    write_all(2, b, (size_t)n);
    _exit(1);
}

void *ember_read_file(void *path, i64 line) {
    char p[4096];
    cstr(path, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) io_error("read", path, line);
    size_t cap = 4096, n = 0;
    char *buf = (char *)malloc(cap);
    size_t k;
    while ((k = fread(buf + n, 1, cap - n, f)) > 0) {
        n += k;
        if (n == cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
    }
    int bad = ferror(f);
    fclose(f);
    if (bad) io_error("read", path, line);
    void *r = new_str(n);
    memcpy(SDATA(r), buf, n);
    free(buf);
    return r;
}

static void put_file(void *path, void *data, const char *mode, i64 line) {
    char p[4096];
    cstr(path, p, sizeof p);
    FILE *f = fopen(p, mode);
    if (!f) io_error("write", path, line);
    size_t n = (size_t)SLEN(data);
    if (fwrite(SDATA(data), 1, n, f) != n) io_error("write", path, line);
    if (fclose(f) != 0) io_error("write", path, line);
}
void ember_write_file(void *path, void *data, i64 line) { put_file(path, data, "wb", line); }
void ember_append_file(void *path, void *data, i64 line) { put_file(path, data, "ab", line); }

i64 ember_file_exists(void *path) {
    char p[4096];
    cstr(path, p, sizeof p);
    return access(p, F_OK) == 0;
}

void *ember_env(void *name) {
    char n[4096];
    cstr(name, n, sizeof n);
    const char *v = getenv(n);
    size_t len = v ? strlen(v) : 0;
    void *r = new_str(len);
    if (len) memcpy(SDATA(r), v, len);
    return r;
}
i64 ember_has_env(void *name) {
    char n[4096];
    cstr(name, n, sizeof n);
    return getenv(n) != NULL;
}

__attribute__((noreturn)) void ember_exit(i64 code) {
    oflush();
    _exit((int)(code & 255));
}

i64 ember_clock_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

i64 ember_fmath(i64 fn, i64 abits, i64 bbits) {
    double a = bits_to_double(abits), b = bits_to_double(bbits);
    switch (fn) {
        case 0: return double_to_bits(sin(a));
        case 1: return double_to_bits(cos(a));
        case 2: return double_to_bits(tan(a));
        case 3: return double_to_bits(atan(a));
        case 4: return double_to_bits(atan2(a, b));
        case 5: return double_to_bits(exp(a));
        case 6: return double_to_bits(log(a));
        case 7: return double_to_bits(pow(a, b));
        case 8: return double_to_bits(floor(a));
        default: return double_to_bits(ceil(a));
    }
}

/* ===================================================================================== startup */
static i64 g_ret;

static void on_fault(int sig, siginfo_t *si, void *ctx) {
    (void)sig;
    (void)ctx;
    uintptr_t a = (uintptr_t)si->si_addr;
    write_all(1, obuf, olen);
    const char *m = "runtime error: stack overflow\n";
    if (!(stack_lo && a + ((uintptr_t)1 << 20) >= stack_lo && a <= stack_lo + ((uintptr_t)1 << 20)))
        m = "runtime error: invalid memory access\n";
    write_all(2, m, strlen(m));
    _exit(1);
}

static void *thread_main(void *arg) {
    (void)arg;
    static char altstack[1 << 16];
    stack_t ss;
    ss.ss_sp = altstack;
    ss.ss_size = sizeof altstack;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    pthread_attr_t at;
    if (pthread_getattr_np(pthread_self(), &at) == 0) {
        void *addr;
        size_t size;
        if (pthread_attr_getstack(&at, &addr, &size) == 0) stack_lo = (uintptr_t)addr;
        pthread_attr_destroy(&at);
    }
    stack_base = (uintptr_t)__builtin_frame_address(0);
    g_ret = ember_main();
    return NULL;
}

int main(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;
    const char *s = getenv("EMBER_GC_STRESS");
    if (s) gc_stress = atol(s) > 0 ? atol(s) : 1;
    gc_stats = getenv("EMBER_GC_STATS") != NULL;
    gc_full = getenv("EMBER_GC_FULL") != NULL; /* every collection a full one (the collector before generations) */
    const char *hm = getenv("EMBER_HEAP_MB");
    if (hm && atol(hm) > 0) heap_limit = (size_t)atol(hm) << 20;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    static const size_t sizes[] = {(size_t)1 << 30, (size_t)256 << 20, (size_t)64 << 20};
    int started = 0;
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0] && !started; i++) {
        pthread_t th;
        if (pthread_attr_setstacksize(&attr, sizes[i]) == 0 && pthread_create(&th, &attr, thread_main, NULL) == 0) {
            pthread_join(th, NULL);
            started = 1;
        }
    }
    if (!started) thread_main(NULL); /* fall back to the process stack */
    oflush();
    if (gc_stats) {
        size_t n = n_minor + n_major;
        fprintf(stderr,
                "gc: %zu collections (%zu minor, %zu major), pauses: total %.2f ms, max %.3f ms, mean %.3f ms "
                "(minor mean %.3f ms, major mean %.3f ms); peak live %zu KB, heap now %zu KB in %zu blocks + %zu large objects\n",
                n, n_minor, n_major, pause_total * 1e3, pause_max * 1e3, n ? pause_total * 1e3 / (double)n : 0.0,
                n_minor ? pause_minor_total * 1e3 / (double)n_minor : 0.0,
                n_major ? pause_major_total * 1e3 / (double)n_major : 0.0, peak_live >> 10,
                heap_ready ? heap_bytes() >> 10 : 0, blocks_used - nfree_blocks, nlarges);
    }
    return (int)g_ret;
}

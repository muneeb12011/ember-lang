/* hashmap in C: open addressing, linear probing, the same hash and growth policy as std/map.em */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
typedef struct { int64_t *keys, *vals; char *used; int64_t count, mask; } Map;
static int64_t hash(int64_t k) { int64_t h = (int64_t)((uint64_t)k * 0x9E3779B97F4A7C1ULL); return h ^ (h >> 29); }
static int64_t slot(Map *m, int64_t k) { int64_t i = hash(k) & m->mask; while (m->used[i] && m->keys[i] != k) i = (i + 1) & m->mask; return i; }
static void put(Map *m, int64_t k, int64_t v);
static void grow(Map *m) {
    Map o = *m; int64_t cap = (m->mask + 1) * 2;
    m->keys = calloc(cap, 8); m->vals = calloc(cap, 8); m->used = calloc(cap, 1); m->mask = cap - 1; m->count = 0;
    for (int64_t i = 0; i <= o.mask; i++) if (o.used[i]) put(m, o.keys[i], o.vals[i]);
    free(o.keys); free(o.vals); free(o.used);
}
static void put(Map *m, int64_t k, int64_t v) {
    if ((m->count + 1) * 4 > (m->mask + 1) * 3) grow(m);
    int64_t i = slot(m, k);
    if (!m->used[i]) { m->used[i] = 1; m->keys[i] = k; m->count++; }
    m->vals[i] = v;
}
int main(void) {
    Map m = {calloc(16, 8), calloc(16, 8), calloc(16, 1), 0, 15};
    int64_t n = 1000000, hits = 0, sum = 0;
    for (int64_t i = 0; i < n; i++) put(&m, i * 7919 + 13, i);
    for (int64_t i = 0; i < 2 * n; i++) { int64_t s = slot(&m, i * 7919 + 13); if (m.used[s]) { hits++; sum += m.vals[s]; } }
    printf("%lld\n%lld\n%lld\n", (long long)m.count, (long long)hits, (long long)sum);
    return 0;
}

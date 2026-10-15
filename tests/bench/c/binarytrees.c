/* binary-trees in C with malloc/free, the same shape as tests/bench/binarytrees.em */
#include <stdio.h>
#include <stdlib.h>
typedef struct Node { struct Node *l, *r; } Node;
static Node *make(int d) {
    Node *n = malloc(sizeof *n);
    if (d == 0) { n->l = n->r = NULL; return n; }
    n->l = make(d - 1);
    n->r = make(d - 1);
    return n;
}
static long check(Node *n) { return n->l ? 1 + check(n->l) + check(n->r) : 1; }
static void drop(Node *n) { if (n->l) { drop(n->l); drop(n->r); } free(n); }
int main(void) {
    int maxd = 16;
    Node *s = make(maxd + 1); printf("%ld\n", check(s)); drop(s);
    Node *ll = make(maxd);
    for (int d = 4; d <= maxd; d += 2) {
        long iters = 1L << (maxd - d + 4), c = 0;
        for (long i = 0; i < iters; i++) { Node *t = make(d); c += check(t); drop(t); }
        printf("%ld\n", c);
    }
    printf("%ld\n", check(ll));
    return 0;
}

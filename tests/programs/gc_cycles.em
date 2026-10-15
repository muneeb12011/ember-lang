// GC torture: cyclic garbage (rings, self-references, closures that reach themselves through an array cell) must be
// collected. Runs with EMBER_HEAP_MB=16 (gc_cycles.env) while allocating far more than that in total.
struct Ring { id: int, next: Ring, prev: Ring, data: [int] }

fn ring(n: int, seed: int) -> Ring {
    let first = Ring { id: seed, next: null, prev: null, data: new [int](8) };
    let mut last = first;
    for i in 1..n {
        let r = Ring { id: seed + i, next: null, prev: last, data: new [int](8) };
        last.next = r;
        last = r;
    }
    last.next = first;
    first.prev = last;
    return first;
}

fn main() -> int {
    let mut total = 0;
    for round in 0..400 {
        let r = ring(500, round);
        let mut p = r;
        for i in 0..500 {
            total += p.id;
            p = p.next;
        }
        let me = new [fn() -> int](1);
        me[0] = fn() -> int { return len(me); };
        total += me[0]();
        r.data[0] = 1;
    }
    print(total);
    return 0;
}

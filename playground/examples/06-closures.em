import "std/iter.em" as iter;

struct Counter { n: int }

fn make_counter() -> fn() -> int {
    let c = Counter { n: 0 };
    return fn() -> int { c.n += 1; return c.n; };
}

fn main() -> int {
    let next = make_counter();
    next();
    next();
    print("counter: " + str(next()));

    let factor = 3;
    let tripled = iter.map([1, 2, 3, 4], fn(x: int) -> int { return x * factor; });
    let big = iter.count(tripled, fn(x: int) -> bool { return x > 6; });
    print("values above 6: " + str(big));
    return 0;
}

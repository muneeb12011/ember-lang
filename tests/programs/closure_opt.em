// the optimizer must keep calls through function values: their results may be unused but their effects are not;
// leaf functions that call through a value may be inlined, and the indirect call must survive intact
struct Counter { n: int }

fn side(c: Counter) -> fn(int) -> int {
    return fn(x: int) -> int {
        c.n += x;
        return x;
    };
}
fn call0(f: fn() -> int) -> int { return f(); }
fn sum_with(f: fn(int) -> int, n: int) -> int {
    if n == 0 { return 0; }
    return f(n) + sum_with(f, n - 1);
}
fn pick_fn<T>(a: [T]) -> fn(int) -> T { return fn(i: int) -> T { return a[i]; }; }
fn const_fn(v: int) -> fn() -> int { return fn() -> int { return v; }; }

fn main() -> int {
    let c = Counter { n: 0 };
    let f = side(c);
    f(5);
    let unused = f(7);
    for i in 0..3 { f(1); }
    print(c.n);
    print(call0(const_fn(9)) + call0(fn() -> int { return 1; }));
    print(sum_with(fn(x: int) -> int { return x * x; }, 10));
    print(pick_fn(["a", "b", "c"])(1));
    print(pick_fn([1.5, 2.5])(0));
    let k = 3;
    let g = fn(x: int) -> int { return x / k; };
    print(g(10));
    let h = fn(x: int) -> int { return 100 / x; };
    print(h(0));
    return 0;
}

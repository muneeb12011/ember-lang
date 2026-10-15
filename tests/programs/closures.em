// first-class functions: function types, top-level functions as values, anonymous functions, closures (which capture
// by value), calls through values, functions returning functions, generic higher-order functions
struct Counter { n: int }
struct Op { name: str, f: fn(int, int) -> int }

fn add(a: int, b: int) -> int { return a + b; }
fn sub(a: int, b: int) -> int { return a - b; }
fn apply(f: fn(int, int) -> int, x: int, y: int) -> int { return f(x, y); }
fn make_adder(k: int) -> fn(int) -> int { return fn(x: int) -> int { return x + k; }; }
fn compose(f: fn(int) -> int, g: fn(int) -> int) -> fn(int) -> int {
    return fn(x: int) -> int { return g(f(x)); };
}
fn id<T>(x: T) -> T { return x; }
fn twice<T>(f: fn(T) -> T, x: T) -> T { return f(f(x)); }
fn make_counter() -> fn() -> int {
    let c = Counter { n: 0 };
    return fn() -> int {
        c.n += 1;
        return c.n;
    };
}
fn fib_memo(n: int) -> int {
    let memo = new [int](n + 1);
    let mut go: fn(int) -> int = null;
    let cell = new [fn(int) -> int](1);  // a closure cannot capture itself, so recursion goes through an array cell
    cell[0] = fn(k: int) -> int {
        if k < 2 { return k; }
        if memo[k] != 0 { return memo[k]; }
        let v = cell[0](k - 1) + cell[0](k - 2);
        memo[k] = v;
        return v;
    };
    go = cell[0];
    return go(n);
}

fn main() -> int {
    print(apply(add, 2, 3));
    print(apply(sub, 2, 3));
    print(apply(fn(a: int, b: int) -> int { return a * b; }, 6, 7));
    let add5 = make_adder(5);
    print(add5(10));
    print(make_adder(1)(1));
    print(compose(make_adder(1), fn(x: int) -> int { return x * 2; })(4));
    // closures copy what they capture, at creation
    let mut k = 10;
    let f = fn() -> int { return k; };
    k = 20;
    print(f());
    print(k);
    // shared state goes through a reference
    let tick = make_counter();
    tick();
    tick();
    print(tick());
    let other = make_counter();
    print(other());
    let fs = new [fn(int) -> int](3);
    for i in 0..3 { fs[i] = make_adder(i * 100); }
    print(fs[2](7) + fs[0](1));
    print(twice(make_adder(3), 1));
    print(twice(fn(s: str) -> str { return s + "!"; }, "hi"));
    let ident = id<str>;
    print(ident("x"));
    let nested = fn(a: int) -> fn(int) -> int { return fn(b: int) -> int { return a * 10 + b + k; }; };
    print(nested(4)(2));
    let ops = [Op { name: "+", f: add }, Op { name: "-", f: sub }, Op { name: "max", f: fn(a: int, b: int) -> int {
        if a > b { return a; }
        return b;
    } }];
    for i in 0..len(ops) { print(ops[i].name + " " + str(ops[i].f(9, 4))); }
    print(fib_memo(80));
    let same = add5;
    print(same == add5);
    print(make_adder(5) == add5);
    let g: fn(int) -> int = null;
    print(g == null);
    return 0;
}

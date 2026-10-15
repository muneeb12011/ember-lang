fn steps(n: int) -> int {
    let mut x = n;
    let mut c = 0;
    while x != 1 {
        if x % 2 == 0 { x = x / 2; } else { x = 3 * x + 1; }
        c = c + 1;
    }
    return c;
}

fn main() -> int {
    let mut best = 0;
    let mut arg = 0;
    for i in 1..10000 {
        let s = steps(i);
        if s > best { best = s; arg = i; }
    }
    print(arg);
    print(best);
    return 0;
}

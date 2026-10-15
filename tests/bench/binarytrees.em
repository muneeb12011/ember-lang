// the binary-trees benchmark (allocation heavy): a long-lived tree plus millions of short-lived ones, max depth 16
struct Node { left: Node, right: Node }

fn make(d: int) -> Node {
    if d == 0 { return Node { left: null, right: null }; }
    return Node { left: make(d - 1), right: make(d - 1) };
}

fn check(n: Node) -> int {
    if n.left == null { return 1; }
    return 1 + check(n.left) + check(n.right);
}

fn main() -> int {
    let maxd = 16;
    print(check(make(maxd + 1)));
    let long_lived = make(maxd);
    let mut d = 4;
    while d <= maxd {
        let iters = 1 << (maxd - d + 4);
        let mut c = 0;
        for i in 0..iters { c += check(make(d)); }
        print(c);
        d += 2;
    }
    print(check(long_lived));
    return 0;
}

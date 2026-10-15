fn newton_sqrt(x: float) -> float {
    let mut g = x / 2.0;
    for i in 0..30 { g = (g + x / g) / 2.0; }
    return g;
}

fn main() -> int {
    print(newton_sqrt(2.0));
    print(sqrt(2.0));
    print(float(7) / 2.0);
    print(int(3.99));
    print(int(-3.99));
    print(-1.5 * 2.0);
    print(0.1 + 0.2);
    print(1.0e3 + 2.5e-1);
    let nan = 0.0 / 0.0;
    print(nan == nan);
    print(nan != nan);
    print(nan < 1.0);
    print(nan >= 1.0);
    print(1.0 < 2.0);
    print(2.0 <= 2.0);
    print(3.0 > 2.0);
    print(3.0 >= 4.0);
    print(1.0 / 0.0);
    print(int(1.0 / 0.0));
    print(int(nan));
    let xs = new_floats(4);
    for i in 0..len(xs) { xs[i] = float(i) * 1.5; }
    let mut s = 0.0;
    for i in 0..len(xs) { s = s + xs[i]; }
    print(s);
    print(len(xs));
    return 0;
}

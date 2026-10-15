// std/math.em - integer and float helpers: `import "std/math.em" as math;` then math.abs(x), math.PI.
pub const PI = 3.141592653589793;
pub const E = 2.718281828459045;

pub fn abs(x: int) -> int {
    if x < 0 { return -x; }
    return x;
}

pub fn sign(x: int) -> int {
    if x < 0 { return -1; }
    if x > 0 { return 1; }
    return 0;
}

pub fn min(a: int, b: int) -> int {
    if a < b { return a; }
    return b;
}

pub fn max(a: int, b: int) -> int {
    if a > b { return a; }
    return b;
}

pub fn clamp(x: int, lo: int, hi: int) -> int {
    if x < lo { return lo; }
    if x > hi { return hi; }
    return x;
}

// result is always in 0..m-1 (unlike %, which keeps the sign of the dividend); m must be positive
pub fn mod(a: int, m: int) -> int {
    let r = a % m;
    if r < 0 { return r + m; }
    return r;
}

pub fn gcd(a: int, b: int) -> int {
    let mut x = abs(a);
    let mut y = abs(b);
    while y != 0 {
        let t = x % y;
        x = y;
        y = t;
    }
    return x;
}

pub fn lcm(a: int, b: int) -> int {
    if a == 0 || b == 0 { return 0; }
    return abs(a / gcd(a, b) * b);
}

// base^exp by repeated squaring (wraps on overflow); exp must not be negative
pub fn pow(base: int, exp: int) -> int {
    assert(exp >= 0);
    let mut result = 1;
    let mut b = base;
    let mut e = exp;
    while e > 0 {
        if e & 1 == 1 { result *= b; }
        b *= b;
        e >>= 1;
    }
    return result;
}

// floor of the square root, exact for every non-negative int
pub fn isqrt(n: int) -> int {
    assert(n >= 0);
    if n < 2 { return n; }
    let mut r = int(sqrt(float(n)));
    while r * r > n { r -= 1; }
    while (r + 1) * (r + 1) <= n && (r + 1) * (r + 1) > 0 { r += 1; }
    return r;
}

pub fn is_prime(n: int) -> bool {
    if n < 2 { return false; }
    if n < 4 { return true; }
    if n % 2 == 0 { return false; }
    let mut d = 3;
    while d * d <= n {
        if n % d == 0 { return false; }
        d += 2;
    }
    return true;
}

pub fn fabs(x: float) -> float {
    if x < 0.0 { return -x; }
    return x;
}

pub fn fmin(a: float, b: float) -> float {
    if a < b { return a; }
    return b;
}

pub fn fmax(a: float, b: float) -> float {
    if a > b { return a; }
    return b;
}

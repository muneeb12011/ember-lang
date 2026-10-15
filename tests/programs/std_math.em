import "std/math.em" as math;

fn main() -> int {
    print(math.abs(-7));
    print(math.abs(7));
    print(math.sign(-3));
    print(math.sign(0));
    print(math.min(3, 9));
    print(math.max(3, 9));
    print(math.clamp(15, 0, 10));
    print(math.clamp(-5, 0, 10));
    print(math.clamp(5, 0, 10));
    print(math.mod(-7, 5));
    print(math.mod(7, 5));
    print(math.mod(-10, 5));
    print(math.gcd(84, 36));
    print(math.gcd(-84, 36));
    print(math.gcd(0, 5));
    print(math.lcm(4, 6));
    print(math.lcm(0, 6));
    print(math.pow(3, 13));
    print(math.pow(2, 62));
    print(math.pow(7, 0));
    print(math.isqrt(0));
    print(math.isqrt(99));
    print(math.isqrt(100));
    print(math.isqrt(1000000007 * 3));
    let mut primes = 0;
    for i in 0..200 { if math.is_prime(i) { primes += 1; } }
    print(primes);
    print(math.fabs(-2.5));
    print(math.fmin(1.5, 2.5));
    print(math.fmax(1.5, 2.5));
    print(math.PI);
    print(math.E * 2.0);
    return 0;
}

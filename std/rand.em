// std/rand.em - a small deterministic pseudo-random generator (64-bit LCG, the high bits are used). The same seed
// always gives the same sequence on every machine. Not suitable for cryptography.
pub struct Rng { state: int }

pub fn make(seed: int) -> Rng { return Rng { state: seed }; }

// a uniformly distributed int in 0..2^31-1
pub fn next(r: Rng) -> int {
    r.state = r.state * 6364136223846793005 + 1442695040888963407;
    return (r.state >> 33) & 0x7FFFFFFF;
}

// 0..n-1 (n must be positive)
pub fn range(r: Rng, n: int) -> int { return next(r) % n; }

// a float in [0, 1)
pub fn next_float(r: Rng) -> float { return float(next(r)) / 2147483648.0; }

// Fisher-Yates
pub fn shuffle(r: Rng, a: [int]) {
    let mut i = len(a) - 1;
    while i > 0 {
        let j = range(r, i + 1);
        let t = a[i];
        a[i] = a[j];
        a[j] = t;
        i -= 1;
    }
}

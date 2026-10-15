// std/compat/math.em - DEPRECATED. The prefixed names std/math.em used before Ember had modules, kept for one
// release. Brought in automatically by `import "std/math.em";` (without `as`); new code should write
// `import "std/math.em" as math;` and call the new names.
import "std/math.em" as modern;

pub fn math_abs(x: int) -> int { return modern.abs(x); }
pub fn math_sign(x: int) -> int { return modern.sign(x); }
pub fn math_min(a: int, b: int) -> int { return modern.min(a, b); }
pub fn math_max(a: int, b: int) -> int { return modern.max(a, b); }
pub fn math_clamp(x: int, lo: int, hi: int) -> int { return modern.clamp(x, lo, hi); }
pub fn math_mod(a: int, m: int) -> int { return modern.mod(a, m); }
pub fn math_gcd(a: int, b: int) -> int { return modern.gcd(a, b); }
pub fn math_lcm(a: int, b: int) -> int { return modern.lcm(a, b); }
pub fn math_pow(base: int, exp: int) -> int { return modern.pow(base, exp); }
pub fn math_isqrt(n: int) -> int { return modern.isqrt(n); }
pub fn math_is_prime(n: int) -> bool { return modern.is_prime(n); }
pub fn math_fabs(x: float) -> float { return modern.fabs(x); }
pub fn math_fmin(a: float, b: float) -> float { return modern.fmin(a, b); }
pub fn math_fmax(a: float, b: float) -> float { return modern.fmax(a, b); }

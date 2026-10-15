pub struct Pair { a: int, b: int }

pub const BASE = 100;

pub fn util_pair(a: int, b: int) -> Pair { return Pair { a: a, b: b }; }
pub fn util_sum(p: Pair) -> int { return p.a + p.b + BASE; }

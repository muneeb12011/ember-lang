pub const VERSION = 2;
pub struct Item { id: int, weight: float }

fn helper(x: int) -> int { return x * 100; }
pub fn name() -> str { return "two"; }
pub fn scaled(x: int) -> int { return helper(x) + VERSION; }

// expect: struct `Hidden` is private to
import "mods/m.em" as m;
fn main() -> int { let h = m.Hidden { k: 1 }; return h.k; }

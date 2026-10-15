// expect: struct `Hidden` is private to
import "mods/m.em" as m;
fn f(h: m.Hidden) -> int { return 0; }
fn main() -> int { return 0; }

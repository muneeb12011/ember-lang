// expect: module name `m` is already used
import "mods/m.em" as m;
import "mods/n.em" as m;
fn main() -> int { return 0; }

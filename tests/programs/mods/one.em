// two modules (one.em, two.em) define the same names; neither clashes with the other or with the importer
pub const VERSION = 1;
pub struct Item { id: int, label: str }

fn helper(x: int) -> int { return x * 10; }
pub fn name() -> str { return "one"; }
pub fn scaled(x: int) -> int { return helper(x) + VERSION; }
pub fn item(id: int) -> Item { return Item { id: id, label: name() + "#" + str(id) }; }
struct Secret { k: int }
pub fn secret_value() -> int { let s = Secret { k: 7 }; return s.k; }

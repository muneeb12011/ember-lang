// std/compat/map.em - DEPRECATED. The prefixed names std/map.em used before Ember had modules, kept for one
// release. Brought in automatically by `import "std/map.em";` (without `as`); new code should write
// `import "std/map.em" as map;` and call the new names.
import "std/map.em" as modern;

pub fn map_new() -> modern.IntMap { return modern.make(); }
pub fn map_len(m: modern.IntMap) -> int { return modern.size(m); }
pub fn map_put(m: modern.IntMap, k: int, v: int) { modern.put(m, k, v); }
pub fn map_has(m: modern.IntMap, k: int) -> bool { return modern.has(m, k); }
pub fn map_get(m: modern.IntMap, k: int, default: int) -> int { return modern.get(m, k, default); }
pub fn map_keys(m: modern.IntMap) -> [int] { return modern.keys(m); }

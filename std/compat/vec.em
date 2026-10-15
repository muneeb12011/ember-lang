// std/compat/vec.em - DEPRECATED. The prefixed names std/vec.em used before Ember had modules and generics, kept for
// one release. Brought in automatically by `import "std/vec.em";` (without `as`). The old types IntVec and StrVec
// are now vec.Vec<int> and vec.Vec<str>; new code should write `import "std/vec.em" as vec;` and use the new names.
import "std/vec.em" as modern;

pub fn vec_new() -> modern.Vec<int> { return modern.make(); }
pub fn vec_from_array(a: [int]) -> modern.Vec<int> { return modern.from_array(a); }
pub fn vec_len(v: modern.Vec<int>) -> int { return modern.size(v); }
pub fn vec_push(v: modern.Vec<int>, x: int) { modern.push(v, x); }
pub fn vec_get(v: modern.Vec<int>, i: int) -> int { return modern.get(v, i); }
pub fn vec_set(v: modern.Vec<int>, i: int, x: int) { modern.set(v, i, x); }
pub fn vec_pop(v: modern.Vec<int>) -> int { return modern.pop(v); }
pub fn vec_clear(v: modern.Vec<int>) { modern.clear(v); }
pub fn vec_to_array(v: modern.Vec<int>) -> [int] { return modern.to_array(v); }
pub fn vec_index_of(v: modern.Vec<int>, x: int) -> int { return modern.index_of(v, x); }
pub fn vec_contains(v: modern.Vec<int>, x: int) -> bool { return modern.contains(v, x); }
pub fn vec_reverse(v: modern.Vec<int>) { modern.reverse(v); }
pub fn vec_sum(v: modern.Vec<int>) -> int { return modern.sum(v); }
pub fn strvec_new() -> modern.Vec<str> { return modern.make(); }
pub fn strvec_len(v: modern.Vec<str>) -> int { return modern.size(v); }
pub fn strvec_push(v: modern.Vec<str>, s: str) { modern.push(v, s); }
pub fn strvec_get(v: modern.Vec<str>, i: int) -> str { return modern.get(v, i); }
pub fn strvec_to_array(v: modern.Vec<str>) -> [str] { return modern.to_array(v); }

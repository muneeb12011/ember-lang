// std/compat/sort.em - DEPRECATED. The prefixed names std/sort.em used before Ember had modules, kept for one
// release. Brought in automatically by `import "std/sort.em";` (without `as`); new code should write
// `import "std/sort.em" as sort;` and call the new names.
import "std/sort.em" as modern;

pub fn sort_ints(a: [int]) { modern.ints(a); }
pub fn sort_is_sorted(a: [int]) -> bool { return modern.is_sorted(a); }
pub fn sort_search(a: [int], x: int) -> int { return modern.search(a, x); }
pub fn sort_floats(a: [float]) { modern.floats(a); }
pub fn sort_strs(a: [str]) { modern.strs(a); }

// std/compat/str.em - DEPRECATED. The prefixed names std/str.em used before Ember had modules, kept for one
// release. Brought in automatically by `import "std/str.em";` (without `as`); new code should write
// `import "std/str.em" as str;` and call the new names.
import "std/str.em" as modern;

pub fn str_is_digit(c: int) -> bool { return modern.is_digit(c); }
pub fn str_is_alpha(c: int) -> bool { return modern.is_alpha(c); }
pub fn str_is_space(c: int) -> bool { return modern.is_space(c); }
pub fn str_matches_at(s: str, t: str, i: int) -> bool { return modern.matches_at(s, t, i); }
pub fn str_starts_with(s: str, p: str) -> bool { return modern.starts_with(s, p); }
pub fn str_ends_with(s: str, p: str) -> bool { return modern.ends_with(s, p); }
pub fn str_find(s: str, t: str) -> int { return modern.find(s, t); }
pub fn str_contains(s: str, t: str) -> bool { return modern.contains(s, t); }
pub fn str_count(s: str, t: str) -> int { return modern.count(s, t); }
pub fn str_repeat(s: str, n: int) -> str { return modern.repeat(s, n); }
pub fn str_reverse(s: str) -> str { return modern.reverse(s); }
pub fn str_upper(s: str) -> str { return modern.upper(s); }
pub fn str_lower(s: str) -> str { return modern.lower(s); }
pub fn str_trim(s: str) -> str { return modern.trim(s); }
pub fn str_pad_left(s: str, width: int, ch: int) -> str { return modern.pad_left(s, width, ch); }
pub fn str_replace(s: str, from: str, to: str) -> str { return modern.replace(s, from, to); }
pub fn str_split(s: str, sep: str) -> [str] { return modern.split(s, sep); }
pub fn str_join(parts: [str], sep: str) -> str { return modern.join(parts, sep); }

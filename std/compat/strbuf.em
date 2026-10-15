// std/compat/strbuf.em - DEPRECATED. The prefixed names std/strbuf.em used before Ember had modules, kept for one
// release. Brought in automatically by `import "std/strbuf.em";` (without `as`); new code should write
// `import "std/strbuf.em" as strbuf;` and call the new names.
import "std/strbuf.em" as modern;

pub fn sb_new() -> modern.StrBuf { return modern.make(); }
pub fn sb_len(b: modern.StrBuf) -> int { return modern.size(b); }
pub fn sb_clear(b: modern.StrBuf) { modern.clear(b); }
pub fn sb_reserve(b: modern.StrBuf, extra: int) { modern.reserve(b, extra); }
pub fn sb_push_char(b: modern.StrBuf, c: int) { modern.push_char(b, c); }
pub fn sb_push(b: modern.StrBuf, s: str) { modern.push(b, s); }
pub fn sb_push_int(b: modern.StrBuf, x: int) { modern.push_int(b, x); }
pub fn sb_to_str(b: modern.StrBuf) -> str { return modern.to_str(b); }

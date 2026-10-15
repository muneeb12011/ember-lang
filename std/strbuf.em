// std/strbuf.em - string builder. Appending to a str with + copies it every time (quadratic); a StrBuf keeps a
// growing byte buffer, so building a string of n bytes costs O(n).
pub struct StrBuf { data: [byte], len: int }

pub fn make() -> StrBuf {
    return StrBuf { data: new [byte](16), len: 0 };
}

pub fn size(b: StrBuf) -> int { return b.len; }
pub fn clear(b: StrBuf) { b.len = 0; }

pub fn reserve(b: StrBuf, extra: int) {
    let need = b.len + extra;
    if need <= len(b.data) { return; }
    let mut cap = len(b.data);
    while cap < need { cap *= 2; }
    let bigger = new [byte](cap);
    for i in 0..b.len { bigger[i] = b.data[i]; }
    b.data = bigger;
}

pub fn push_char(b: StrBuf, c: int) {
    reserve(b, 1);
    b.data[b.len] = c;
    b.len += 1;
}

pub fn push(b: StrBuf, s: str) {
    let n = len(s);
    reserve(b, n);
    for i in 0..n { b.data[b.len + i] = s[i]; }
    b.len += n;
}

pub fn push_int(b: StrBuf, x: int) { push(b, str(x)); }

pub fn to_str(b: StrBuf) -> str { return str_from_bytes(b.data, b.len); }

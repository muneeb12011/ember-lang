// std/str.em - string helpers (ASCII). Every function returns a new str; strs are immutable.
pub fn is_digit(c: int) -> bool { return c >= 48 && c <= 57; }
pub fn is_alpha(c: int) -> bool { return (c >= 65 && c <= 90) || (c >= 97 && c <= 122); }
pub fn is_space(c: int) -> bool { return c == 32 || (c >= 9 && c <= 13); }

// does t occur in s starting exactly at index i?
pub fn matches_at(s: str, t: str, i: int) -> bool {
    let m = len(t);
    if i < 0 || i + m > len(s) { return false; }
    for k in 0..m { if s[i + k] != t[k] { return false; } }
    return true;
}

pub fn starts_with(s: str, p: str) -> bool { return matches_at(s, p, 0); }
pub fn ends_with(s: str, p: str) -> bool { return matches_at(s, p, len(s) - len(p)); }

// index of the first occurrence of t in s, or -1
pub fn find(s: str, t: str) -> int {
    let n = len(s);
    let m = len(t);
    let mut i = 0;
    while i + m <= n {
        if matches_at(s, t, i) { return i; }
        i += 1;
    }
    return -1;
}

pub fn contains(s: str, t: str) -> bool { return find(s, t) >= 0; }

// number of non-overlapping occurrences of t (which must not be empty)
pub fn count(s: str, t: str) -> int {
    assert(len(t) > 0);
    let mut c = 0;
    let mut i = 0;
    while i + len(t) <= len(s) {
        if matches_at(s, t, i) { c += 1; i += len(t); } else { i += 1; }
    }
    return c;
}

pub fn repeat(s: str, n: int) -> str {
    assert(n >= 0);
    let total = len(s) * n;
    let b = new [byte](total);
    for r in 0..n {
        for k in 0..len(s) { b[r * len(s) + k] = s[k]; }
    }
    return str_from_bytes(b, total);
}

pub fn reverse(s: str) -> str {
    let n = len(s);
    let b = new [byte](n);
    for i in 0..n { b[i] = s[n - 1 - i]; }
    return str_from_bytes(b, n);
}

pub fn upper(s: str) -> str {
    let n = len(s);
    let b = new [byte](n);
    for i in 0..n {
        let c = s[i];
        if c >= 97 && c <= 122 { b[i] = c - 32; } else { b[i] = c; }
    }
    return str_from_bytes(b, n);
}

pub fn lower(s: str) -> str {
    let n = len(s);
    let b = new [byte](n);
    for i in 0..n {
        let c = s[i];
        if c >= 65 && c <= 90 { b[i] = c + 32; } else { b[i] = c; }
    }
    return str_from_bytes(b, n);
}

pub fn trim(s: str) -> str {
    let n = len(s);
    let mut a = 0;
    while a < n && is_space(s[a]) { a += 1; }
    let mut e = n;
    while e > a && is_space(s[e - 1]) { e -= 1; }
    return substr(s, a, e - a);
}

// pad on the left with the character ch until the result is `width` long
pub fn pad_left(s: str, width: int, ch: int) -> str {
    let n = len(s);
    if n >= width { return s; }
    let b = new [byte](width);
    let pad = width - n;
    for i in 0..pad { b[i] = ch; }
    for i in 0..n { b[pad + i] = s[i]; }
    return str_from_bytes(b, width);
}

pub fn replace(s: str, from: str, to: str) -> str {
    assert(len(from) > 0);
    let cnt = count(s, from);
    if cnt == 0 { return s; }
    let total = len(s) + cnt * (len(to) - len(from));
    let b = new [byte](total);
    let mut i = 0;
    let mut pos = 0;
    while i < len(s) {
        if matches_at(s, from, i) {
            for k in 0..len(to) { b[pos + k] = to[k]; }
            pos += len(to);
            i += len(from);
        } else {
            b[pos] = s[i];
            pos += 1;
            i += 1;
        }
    }
    return str_from_bytes(b, total);
}

// split on every occurrence of sep (which must not be empty); "a,b,,c" gives ["a", "b", "", "c"]
pub fn split(s: str, sep: str) -> [str] {
    assert(len(sep) > 0);
    let out = new [str](count(s, sep) + 1);
    let mut start = 0;
    let mut k = 0;
    let mut i = 0;
    while i + len(sep) <= len(s) {
        if matches_at(s, sep, i) {
            out[k] = substr(s, start, i - start);
            k += 1;
            i += len(sep);
            start = i;
        } else {
            i += 1;
        }
    }
    out[k] = substr(s, start, len(s) - start);
    return out;
}

pub fn join(parts: [str], sep: str) -> str {
    let n = len(parts);
    if n == 0 { return ""; }
    let mut total = len(sep) * (n - 1);
    for i in 0..n { total += len(parts[i]); }
    let b = new [byte](total);
    let mut pos = 0;
    for i in 0..n {
        if i > 0 {
            for k in 0..len(sep) { b[pos + k] = sep[k]; }
            pos += len(sep);
        }
        let p = parts[i];
        for k in 0..len(p) { b[pos + k] = p[k]; }
        pos += len(p);
    }
    return str_from_bytes(b, total);
}

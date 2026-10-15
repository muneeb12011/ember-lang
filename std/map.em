// std/map.em - IntMap: a hash map from int to int. Open addressing with linear probing, power-of-two capacity,
// grows at 75% load. (No removal: it would need tombstones.)
pub struct IntMap { keys: [int], vals: [int], used: [bool], count: int, mask: int }

pub fn make() -> IntMap {
    return IntMap { keys: new [int](16), vals: new [int](16), used: new [bool](16), count: 0, mask: 15 };
}

pub fn size(m: IntMap) -> int { return m.count; }

fn hash(k: int) -> int {
    let h = k * 0x9E3779B97F4A7C1;
    return h ^ (h >> 29);
}

// the slot holding key k, or the empty slot where it would be inserted
fn slot(m: IntMap, k: int) -> int {
    let mut i = hash(k) & m.mask;
    while m.used[i] && m.keys[i] != k { i = (i + 1) & m.mask; }
    return i;
}

fn grow(m: IntMap) {
    let ok = m.keys;
    let ov = m.vals;
    let ou = m.used;
    let ncap = (m.mask + 1) * 2;
    m.keys = new [int](ncap);
    m.vals = new [int](ncap);
    m.used = new [bool](ncap);
    m.mask = ncap - 1;
    m.count = 0;
    for i in 0..len(ok) {
        if ou[i] { put(m, ok[i], ov[i]); }
    }
}

pub fn put(m: IntMap, k: int, v: int) {
    if (m.count + 1) * 4 > (m.mask + 1) * 3 { grow(m); }
    let i = slot(m, k);
    if !m.used[i] {
        m.used[i] = true;
        m.keys[i] = k;
        m.count += 1;
    }
    m.vals[i] = v;
}

pub fn has(m: IntMap, k: int) -> bool { return m.used[slot(m, k)]; }

pub fn get(m: IntMap, k: int, default: int) -> int {
    let i = slot(m, k);
    if m.used[i] { return m.vals[i]; }
    return default;
}

// all keys, in no particular order
pub fn keys(m: IntMap) -> [int] {
    let out = new [int](m.count);
    let mut n = 0;
    for i in 0..len(m.keys) {
        if m.used[i] {
            out[n] = m.keys[i];
            n += 1;
        }
    }
    return out;
}

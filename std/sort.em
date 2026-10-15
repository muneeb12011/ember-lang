// std/sort.em - quicksort (median of three, insertion sort below 17 elements, recursion only into the smaller
// side so the stack stays O(log n)) for [int] and [float]; stable merge sort for [str]; binary search.
fn insertion(a: [int], lo: int, hi: int) {
    for i in lo + 1..hi + 1 {
        let key = a[i];
        let mut j = i - 1;
        while j >= lo && a[j] > key {
            a[j + 1] = a[j];
            j -= 1;
        }
        a[j + 1] = key;
    }
}

fn range(a: [int], lo0: int, hi0: int) {
    let mut lo = lo0;
    let mut hi = hi0;
    while hi - lo > 16 {
        let x = a[lo];
        let y = a[lo + (hi - lo) / 2];
        let z = a[hi];
        let mut pivot = z;
        if (x <= y && y <= z) || (z <= y && y <= x) { pivot = y; }
        else if (y <= x && x <= z) || (z <= x && x <= y) { pivot = x; }
        let mut i = lo;
        let mut j = hi;
        while i <= j {
            while a[i] < pivot { i += 1; }
            while a[j] > pivot { j -= 1; }
            if i <= j {
                let t = a[i];
                a[i] = a[j];
                a[j] = t;
                i += 1;
                j -= 1;
            }
        }
        if j - lo < hi - i {
            range(a, lo, j);
            lo = i;
        } else {
            range(a, i, hi);
            hi = j;
        }
    }
    insertion(a, lo, hi);
}

pub fn ints(a: [int]) {
    if len(a) > 1 { range(a, 0, len(a) - 1); }
}

pub fn is_sorted(a: [int]) -> bool {
    for i in 1..len(a) { if a[i - 1] > a[i] { return false; } }
    return true;
}

// index of x in the sorted array a, or -1
pub fn search(a: [int], x: int) -> int {
    let mut lo = 0;
    let mut hi = len(a) - 1;
    while lo <= hi {
        let mid = lo + (hi - lo) / 2;
        if a[mid] == x { return mid; }
        if a[mid] < x { lo = mid + 1; } else { hi = mid - 1; }
    }
    return -1;
}

fn insertion_f(a: [float], lo: int, hi: int) {
    for i in lo + 1..hi + 1 {
        let key = a[i];
        let mut j = i - 1;
        while j >= lo && a[j] > key {
            a[j + 1] = a[j];
            j -= 1;
        }
        a[j + 1] = key;
    }
}

fn range_f(a: [float], lo0: int, hi0: int) {
    let mut lo = lo0;
    let mut hi = hi0;
    while hi - lo > 16 {
        let x = a[lo];
        let y = a[lo + (hi - lo) / 2];
        let z = a[hi];
        let mut pivot = z;
        if (x <= y && y <= z) || (z <= y && y <= x) { pivot = y; }
        else if (y <= x && x <= z) || (z <= x && x <= y) { pivot = x; }
        let mut i = lo;
        let mut j = hi;
        while i <= j {
            while a[i] < pivot { i += 1; }
            while a[j] > pivot { j -= 1; }
            if i <= j {
                let t = a[i];
                a[i] = a[j];
                a[j] = t;
                i += 1;
                j -= 1;
            }
        }
        if j - lo < hi - i {
            range_f(a, lo, j);
            lo = i;
        } else {
            range_f(a, i, hi);
            hi = j;
        }
    }
    insertion_f(a, lo, hi);
}

// NaN is not ordered, so an array containing one has no meaningful sorted order
pub fn floats(a: [float]) {
    if len(a) > 1 { range_f(a, 0, len(a) - 1); }
}

fn merge_strs(a: [str], tmp: [str], lo: int, hi: int) {
    if hi - lo < 2 { return; }
    let mid = lo + (hi - lo) / 2;
    merge_strs(a, tmp, lo, mid);
    merge_strs(a, tmp, mid, hi);
    let mut i = lo;
    let mut j = mid;
    let mut k = lo;
    while i < mid && j < hi {
        if a[j] < a[i] { tmp[k] = a[j]; j += 1; } else { tmp[k] = a[i]; i += 1; }
        k += 1;
    }
    while i < mid { tmp[k] = a[i]; i += 1; k += 1; }
    while j < hi { tmp[k] = a[j]; j += 1; k += 1; }
    for t in lo..hi { a[t] = tmp[t]; }
}

pub fn strs(a: [str]) {
    merge_strs(a, new [str](len(a)), 0, len(a));
}

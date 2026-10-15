fn main() -> int {
    let mut count = 0;
    for i in 0..30 {
        if i % 3 == 0 { continue; }
        let mut j = i + 1;
        while true {
            if j >= 30 { break; }
            if j % 5 == 0 { j = j + 1; continue; }
            if (i * j) % 7 == 0 { count = count + 1; }
            j = j + 1;
        }
    }
    print(count);
    return 0;
}

// building a 20 MB string from 3,000,000 numbers with std/strbuf, then scanning it
import "std/strbuf.em" as strbuf;

fn main() -> int {
    let b = strbuf.make();
    for i in 0..3000000 {
        strbuf.push_int(b, i);
        strbuf.push_char(b, 44);
    }
    let s = strbuf.to_str(b);
    let mut commas = 0;
    let mut digits = 0;
    for i in 0..len(s) {
        if s[i] == 44 { commas += 1; } else { digits += s[i] - 48; }
    }
    print(len(s));
    print(commas);
    print(digits);
    return 0;
}

import "std/strbuf.em" as strbuf;

fn main() -> int {
    let b = strbuf.make();
    for i in 0..100 {
        strbuf.push(b, str(i));
        strbuf.push_char(b, 44);
    }
    print(strbuf.size(b));
    let s = strbuf.to_str(b);
    print(len(s));
    print(substr(s, 0, 20));
    print(s[strbuf.size(b) - 1]);
    strbuf.clear(b);
    strbuf.push(b, "reset");
    print(strbuf.to_str(b));
    let big = strbuf.make();
    for i in 0..20000 { strbuf.push_int(big, i % 10); }
    print(strbuf.size(big));
    print(substr(strbuf.to_str(big), 19990, 10));
    return 0;
}

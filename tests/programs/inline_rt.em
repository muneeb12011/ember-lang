// regression: the inliner renumbered registers in a/b/c but not in the argument list of runtime calls, so an
// inlined leaf such as this `f` read the caller's registers instead of its own parameters (found via std_vec -O1)
struct V { data: [str], len: int }

fn f(v: V) -> [str] {
    let out = new [str](v.len);
    for i in 0..v.len { out[i] = v.data[i]; }
    return out;
}
fn cat(a: str, b: str) -> str { return a + b; }
fn twice(s: str) -> str { return cat(s, s); }
fn tag(n: int) -> str { return "q" + str(n); }

fn main() -> int {
    let s = V { data: new [str](4), len: 3 };
    print(len(f(s)));
    print(cat("ab", "c"));
    print(twice("xyz"));
    print(len(cat("ab", "cde")));
    print(tag(-3) == "q-3");
    print(tag(-3));
    return 0;
}

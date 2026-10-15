fn t(s: str) -> bool { print(s); return true; }
fn f(s: str) -> bool { print(s); return false; }

fn main() -> int {
    if f("a") && t("b") { print("1"); }
    if t("c") || f("d") { print("2"); }
    if f("e") || t("f") { print("3"); }
    if !(t("g") && f("h")) { print("4"); }
    let x = t("i") && t("j");
    let y = f("k") || f("l");
    print(x);
    print(y);
    return 0;
}

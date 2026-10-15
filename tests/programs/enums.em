// enums with payloads, exhaustive match, recursive enums, enums inside structs and arrays
enum Shape { Circle(float), Rect(float, float), Empty }
enum Expr { Num(int), Add(Expr, Expr), Mul(Expr, Expr), Neg(Expr), Var(str) }
enum Token { Word(str), Number(int), Flag(bool), End }
struct Labeled { name: str, shape: Shape }

fn area(s: Shape) -> float {
    match s {
        Circle(r) => { return 3.0 * r * r; }
        Rect(w, h) => { return w * h; }
        Empty => { return 0.0; }
    }
}

fn eval(e: Expr, x: int) -> int {
    match e {
        Num(n) => { return n; }
        Add(a, b) => { return eval(a, x) + eval(b, x); }
        Mul(a, b) => { return eval(a, x) * eval(b, x); }
        Neg(a) => { return -eval(a, x); }
        Var(_) => { return x; }
    }
}

fn show(e: Expr) -> str {
    match e {
        Num(n) => { return str(n); }
        Add(a, b) => { return "(" + show(a) + " + " + show(b) + ")"; }
        Mul(a, b) => { return show(a) + " * " + show(b); }
        Neg(a) => { return "-" + show(a); }
        Var(name) => { return name; }
    }
}

fn describe(t: Token) -> str {
    match t {
        Word(w) => { return "word " + w; }
        Number(n) => {
            if n < 0 { return "negative"; }
            return "number " + str(n);
        }
        _ => { return "other"; }
    }
}

fn is_round(s: Shape) -> bool {
    match s {
        Circle(_) => { return true; }
        _ => { return false; }
    }
}

fn main() -> int {
    print(area(Shape.Circle(2.0)));
    print(area(Shape.Rect(2.0, 3.5)));
    print(area(Shape.Empty));
    let e = Expr.Add(Expr.Num(2), Expr.Mul(Expr.Var("x"), Expr.Neg(Expr.Num(4))));
    print(show(e));
    print(eval(e, 5));
    // build a deep expression: 1 + (2 + (3 + ...))
    let mut deep = Expr.Num(0);
    for i in 1..200 { deep = Expr.Add(Expr.Num(i), deep); }
    print(eval(deep, 0));
    let toks = new [Token](5);
    toks[0] = Token.Word("hi");
    toks[1] = Token.Number(42);
    toks[2] = Token.Number(-1);
    toks[3] = Token.Flag(true);
    toks[4] = Token.End;
    for i in 0..len(toks) { print(describe(toks[i])); }
    let shapes = new [Labeled](3);
    shapes[0] = Labeled { name: "c", shape: Shape.Circle(1.0) };
    shapes[1] = Labeled { name: "r", shape: Shape.Rect(1.0, 2.0) };
    shapes[2] = Labeled { name: "e", shape: Shape.Empty };
    let mut total = 0.0;
    let mut round = 0;
    for i in 0..3 {
        total = total + area(shapes[i].shape);
        if is_round(shapes[i].shape) { round += 1; }
    }
    print(total);
    print(round);
    let a = Shape.Empty;
    let b = a;
    print(a == b);
    print(Shape.Empty == Shape.Empty);
    let mut m: Shape = null;
    print(m == null);
    m = Shape.Circle(0.5);
    print(area(m));
    return 0;
}

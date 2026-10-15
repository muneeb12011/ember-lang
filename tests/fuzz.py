#!/usr/bin/env python3
"""Differential fuzzer for the Ember compiler.

Generates random, valid, terminating, deterministic Ember programs that cover the whole language - ints, floats,
bools, strs (+, comparisons, len, indexing, substr, str()), [int] [byte] [bool] [str] arrays, bitwise operators and
shifts, structs with null, multi-function programs, and calls into the standard library - and runs each one four
ways: IR interpreter and native x86-64, at -O0 and -O1 (and the wasm target under node when node and clang are installed; EMBER_FUZZ_WASM=0 turns that
off). All paths must agree on stdout, stderr and exit code. Runtime
traps (index out of range, division by zero, null dereference, failed assert, panic, bad substr, negative size) are
generated on purpose, so the message and line of the error are compared too.

A disagreement is a compiler or runtime bug. The program is shrunk (statements deleted, expressions simplified while
the disagreement remains) and the minimal version is saved to tests/fuzz_failures/.

    python3 tests/fuzz.py [count] [first_seed] [--jobs N] [--no-shrink]
    python3 tests/fuzz.py --shrink file.em          shrink an existing failing program
"""
import concurrent.futures as cf
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EMBC = os.environ.get("EMBER_EMBC") or os.path.join(ROOT, "embc")  # EMBER_EMBC: fuzz a frozen copy of the compiler
FAILDIR = os.path.join(ROOT, "tests", "fuzz_failures")

INT, BOOL, FLOAT, STR, S = "int", "bool", "float", "str", "S"
AI, AY, AB, AS = "[int]", "[byte]", "[bool]", "[str]"
SCALARS = [INT, BOOL, FLOAT, STR]
ARRAYS = [AI, AY, AB, AS]
ELEM = {AI: INT, AY: INT, AB: BOOL, AS: STR}
NEWT = {AI: "int", AY: "byte", AB: "bool", AS: "str"}
STRLITS = ['""', '"a"', '"hello"', '"x,y,,z"', '"A b C"', '" pad "', '"\\t"', '"\\\\"', '"\\"q\\""', '"0123"', '"-42"',
           '"ab,cd"', '"zz"']

PRELUDE = """struct S { a: int, f: float, s: str, next: S }
enum E { A(int), B(str, int), C }
struct Box<T> { v: T, n: int }

fn pick<T>(c: bool, a: T, b: T) -> T {
    if c { return a; }
    return b;
}
fn boxed<T>(x: T) -> Box<T> { return Box { v: x, n: 1 }; }
fn app<T>(f: fn(T) -> T, x: T) -> T { return f(x); }
fn ecode(e: E) -> int {
    match e {
        A(x) => { return x; }
        B(s, n) => { return len(s) + n; }
        C => { return -1; }
    }
}

fn clip(s: str) -> str {
    if len(s) > 40 { return substr(s, 0, 40); }
    return s;
}
"""


class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)
        self.n = 0
        self.funcs = []  # (name, [param types], return type or None)
        self.use_math = self.r.random() < 0.5
        self.use_str = self.r.random() < 0.5
        self.use_coll = self.r.random() < 0.3   # vec, map, sort, strbuf
        self.trap_budget = 1 if self.r.random() < 0.35 else 0

    def fresh(self, p):
        self.n += 1
        return f"{p}{self.n}"

    # ---- scopes: list of dicts name -> {"t": type, "mut": bool, "nn": bool (provably non-null)} -------------
    def vars(self, sc, t, mut=None, nn=None):
        out = []
        seen = set()
        for s in reversed(sc):
            for name, v in s.items():
                if name in seen:
                    continue
                seen.add(name)
                if v["t"] == t and (mut is None or v["mut"] == mut) and (nn is None or v["nn"] == nn):
                    out.append(name)
        return sorted(out)

    def pick(self, xs):
        return self.r.choice(xs) if xs else None

    # ---- expressions -------------------------------------------------------------------------------------------
    def ilit(self):
        r = self.r
        c = r.random()
        if c < 0.04:
            return "9223372036854775807"
        if c < 0.07:
            return "(-9223372036854775807 - 1)"
        if c < 0.12:
            return r.choice(["0xFF", "0x7F", "0x100", "0xDEADBEEF"])
        if c < 0.16:
            return r.choice(["'a'", "'0'", "' '", "'\\n'", "'z'"])
        if c < 0.22:
            return "0"
        return str(r.randint(0, 25))

    def safe_idx(self, n_expr, sc, d):
        e = self.expr(INT, sc, d)
        return f"(({e} % {n_expr} + {n_expr}) % {n_expr})"

    def arr_index(self, a, sc, d):
        if self.trap_budget and self.r.random() < 0.05:
            self.trap_budget -= 1
            return f"({self.expr(INT, sc, d)} % 11)"
        return self.safe_idx("8", sc, d)

    def str_index(self, v, sc, d):  # always in range: index the var with one extra byte appended
        n = f"(len({v}) + 1)"
        return f"({v} + \"#\")[{self.safe_idx(n, sc, d)}]"

    def call_expr(self, t, sc, d):
        cands = [f for f in self.funcs if f[2] == t]
        if not cands:
            return None
        name, ps, _ = self.r.choice(cands)
        return f"{name}({', '.join(self.arg(p, sc, d) for p in ps)})"

    def arg(self, t, sc, d):
        if t in SCALARS:
            return self.expr(t, sc, d)
        if t == S:
            return self.pick(self.vars(sc, S)) or "null"
        return self.pick(self.vars(sc, t)) or f"new [{NEWT[t]}](8)"

    def expr(self, t, sc, d):
        r = self.r
        if d <= 0 or r.random() < 0.2:
            return self.leaf(t, sc)
        for _ in range(4):
            e = getattr(self, "e_" + t)(sc, d - 1)
            if e:
                return e
        return self.leaf(t, sc)

    def leaf(self, t, sc):
        v = self.pick(self.vars(sc, t))
        if v and self.r.random() < 0.7:
            return v
        if t == INT:
            return self.ilit()
        if t == BOOL:
            return self.r.choice(["true", "false"])
        if t == FLOAT:
            return self.r.choice(["0.0", "1.5", "0.1", "2.0", "1000000.0", "0.333", "3.0e10", "1.0e-5"])
        return self.r.choice(STRLITS)

    def e_int(self, sc, d):
        r = self.r
        c = r.random()
        if c < 0.30:
            op = r.choice(["+", "-", "*", "+", "-", "*", "/", "%"])
            a, b = self.expr(INT, sc, d), self.expr(INT, sc, d)
            if op in "/%":
                if self.trap_budget and r.random() < 0.08:
                    self.trap_budget -= 1  # may divide by zero
                else:
                    b = f"({b} % 5 + 7)"
            return f"({a} {op} {b})"
        if c < 0.45:
            op = r.choice(["&", "|", "^", "<<", ">>"])
            return f"({self.expr(INT, sc, d)} {op} {self.expr(INT, sc, d)})"
        if c < 0.50:
            return f"({r.choice(['-', '~'])}{self.expr(INT, sc, d)})"
        if c < 0.58:
            a = self.pick(self.vars(sc, AI) + self.vars(sc, AY))
            if a:
                return f"{a}[{self.arr_index(a, sc, d)}]"
        if c < 0.62:
            a = self.pick([v for t in ARRAYS for v in self.vars(sc, t)])
            if a:
                return f"len({a})"
            return f"len({self.expr(STR, sc, d)})"
        if c < 0.67:
            v = self.pick(self.vars(sc, STR))
            if v:
                return self.str_index(v, sc, d)
            return f"\"hello\"[{self.safe_idx('5', sc, d)}]"
        if c < 0.70:
            return f"int({self.expr(FLOAT, sc, d)})"
        if c < 0.72:
            return f"parse_int({self.expr(STR, sc, d)})"
        if c < 0.77:
            v = self.pick(self.vars(sc, S, nn=True))
            if v:
                return f"{v}.a"
            if self.trap_budget and r.random() < 0.3:
                v = self.pick(self.vars(sc, S))
                if v:
                    self.trap_budget -= 1
                    return f"{v}.next.a"
        if c < 0.87:
            e = self.call_expr(INT, sc, d)
            if e:
                return e
        if c < 0.90:
            k = r.random()
            if k < 0.3:
                return f"pick({self.expr(BOOL, sc, d)}, {self.expr(INT, sc, d)}, {self.expr(INT, sc, d)})"
            if k < 0.5:
                return f"boxed({self.expr(INT, sc, d)}).v"
            if k < 0.62:
                return f"ecode({self.enum_expr(sc, d)})"
            if k < 0.75:  # an anonymous function (capturing whatever it uses), called directly or through app()
                z = self.fresh("z")
                body = self.expr(INT, sc + [{z: {"t": INT, "mut": False, "nn": False}}], d)
                lam = f"fn({z}: int) -> int {{ return {body}; }}"
                if r.random() < 0.5:
                    return f"app({lam}, {self.expr(INT, sc, d)})"
                return f"({lam})({self.expr(INT, sc, d)})"
            return f"[{self.expr(INT, sc, d)}, {self.expr(INT, sc, d)}, {self.expr(INT, sc, d)}][{self.safe_idx('3', sc, d)}]"
        if c < 0.95 and self.use_math:
            x, y = self.expr(INT, sc, d), self.expr(INT, sc, d)
            return r.choice([f"math.abs({x})", f"math.min({x}, {y})", f"math.max({x}, {y})", f"math.sign({x})",
                             f"math.gcd({x}, {y})", f"math.pow({x}, {y} & 15)", f"math.clamp({x}, 0 - 5, 5)",
                             f"math.mod({x}, ({y} & 7) + 1)", f"math.isqrt({x} & 0xFFFFFF)"])
        if self.use_str:
            a = self.expr(STR, sc, d)
            return r.choice([f"strings.find({a}, {r.choice(STRLITS)})", f"strings.count({a}, \"a\")",
                             f"len(strings.split({a}, \",\"))"])
        return None

    def e_bool(self, sc, d):
        r = self.r
        c = r.random()
        if c < 0.30:
            op = r.choice(["<", "<=", ">", ">=", "==", "!="])
            return f"({self.expr(INT, sc, d)} {op} {self.expr(INT, sc, d)})"
        if c < 0.38:
            op = r.choice(["<", "<=", ">", ">=", "==", "!="])
            return f"({self.expr(FLOAT, sc, d)} {op} {self.expr(FLOAT, sc, d)})"
        if c < 0.46:
            op = r.choice(["<", "<=", ">", ">=", "==", "!="])
            return f"({self.expr(STR, sc, d)} {op} {self.expr(STR, sc, d)})"
        if c < 0.58:
            op = r.choice(["&&", "||"])
            return f"({self.expr(BOOL, sc, d)} {op} {self.expr(BOOL, sc, d)})"
        if c < 0.63:
            return f"!{self.expr(BOOL, sc, d)}"
        if c < 0.70:
            a = self.pick(self.vars(sc, AB))
            if a:
                return f"{a}[{self.arr_index(a, sc, d)}]"
        if c < 0.76:
            v = self.pick(self.vars(sc, S))
            if v:
                return f"({v} {r.choice(['==', '!='])} null)"
        if c < 0.86:
            e = self.call_expr(BOOL, sc, d)
            if e:
                return e
        if self.use_str and c < 0.93:
            return f"strings.contains({self.expr(STR, sc, d)}, {r.choice(STRLITS)})"
        if self.use_math:
            return f"math.is_prime({self.expr(INT, sc, d)} & 1023)"
        return None

    def e_float(self, sc, d):
        r = self.r
        c = r.random()
        if c < 0.35:
            op = r.choice(["+", "-", "*", "/"])
            return f"({self.expr(FLOAT, sc, d)} {op} {self.expr(FLOAT, sc, d)})"
        if c < 0.50:
            return f"float({self.expr(INT, sc, d)})"
        if c < 0.55:
            return f"(-{self.expr(FLOAT, sc, d)})"
        if c < 0.70:
            f = r.choice(["sqrt", "floor", "ceil", "sin", "cos", "exp", "atan"])
            return f"{f}({self.expr(FLOAT, sc, d)})"
        if c < 0.74:
            return f"pow({self.expr(FLOAT, sc, d)}, {r.choice(['2.0', '0.5', '3.0'])})"
        if c < 0.80:
            v = self.pick(self.vars(sc, S, nn=True))
            if v:
                return f"{v}.f"
        if c < 0.90:
            e = self.call_expr(FLOAT, sc, d)
            if e:
                return e
        if self.use_math:
            return f"math.fabs({self.expr(FLOAT, sc, d)})"
        return None

    def e_str(self, sc, d):
        r = self.r
        c = r.random()
        if c < 0.25:
            return f"({self.expr(STR, sc, d)} + {self.expr(STR, sc, d)})"
        if c < 0.35:
            v = self.pick(self.vars(sc, STR))
            if v:
                if self.trap_budget and r.random() < 0.1:
                    self.trap_budget -= 1
                    return f"substr({v}, {self.expr(INT, sc, d)} % 4, 2)"
                return f"substr({v}, 0, {self.safe_idx(f'(len({v}) + 1)', sc, d)})"
        if c < 0.50:
            t = r.choice([INT, FLOAT, BOOL])
            return f"str({self.expr(t, sc, d)})"
        if c < 0.55:
            return f"chr(({self.expr(INT, sc, d)} & 63) + 48)"
        if c < 0.62:
            a = self.pick(self.vars(sc, AS))
            if a:
                return f"{a}[{self.arr_index(a, sc, d)}]"
        if c < 0.67:
            v = self.pick(self.vars(sc, S, nn=True))
            if v:
                return f"{v}.s"
        if c < 0.80:
            e = self.call_expr(STR, sc, d)
            if e:
                return e
        if c < 0.84:
            return f"pick({self.expr(BOOL, sc, d)}, {self.expr(STR, sc, d)}, {self.expr(STR, sc, d)})"
        if self.use_str:
            a = self.expr(STR, sc, d)
            return r.choice([f"strings.upper({a})", f"strings.lower({a})", f"strings.reverse({a})", f"strings.trim({a})",
                             f"strings.repeat({a}, {self.expr(INT, sc, d)} & 3)", f"strings.replace({a}, \"a\", \"<>\")",
                             f"strings.pad_left({a}, {self.expr(INT, sc, d)} & 7, 46)",
                             f"strings.join(strings.split({a}, \",\"), \"+\")"])
        return None

    def enum_expr(self, sc, d):
        v = self.pick(self.vars(sc, "E"))
        if v and self.r.random() < 0.6:
            return v
        k = self.r.random()
        if k < 0.4:
            return f"E.A({self.expr(INT, sc, d)})"
        if k < 0.75:
            return f"E.B(clip({self.expr(STR, sc, d)}), {self.expr(INT, sc, d)})"
        return "E.C"

    def clipped(self, t, e):
        return f"clip({e})" if t == STR else e

    # ---- statements --------------------------------------------------------------------------------------------
    def stmts(self, sc, depth, inloop, ind, n=None):
        out = []
        for _ in range(n if n is not None else self.r.randint(1, 5)):
            out += self.stmt(sc, depth, inloop, ind)
        return out

    def block(self, sc, depth, inloop, ind, extra=None):
        sc.append(dict(extra or {}))
        body = self.stmts(sc, depth, inloop, ind + "    ")
        sc.pop()
        return body

    def declare(self, sc, name, t, mut=False, nn=False):
        sc[-1][name] = {"t": t, "mut": mut, "nn": nn}

    def stmt(self, sc, depth, inloop, ind):
        r = self.r
        c = r.random()
        if c < 0.14:
            t = r.choice([INT, INT, BOOL, FLOAT, STR, STR])
            v = self.fresh("v")
            mut = r.random() < 0.7
            line = f"{ind}let {'mut ' if mut else ''}{v} = {self.clipped(t, self.expr(t, sc, 3))};"
            self.declare(sc, v, t, mut)
            return [line]
        if c < 0.30:
            t = r.choice([INT, INT, BOOL, FLOAT, STR])
            v = self.pick(self.vars(sc, t, mut=True))
            if v:
                if t == INT and r.random() < 0.4:
                    op = r.choice(["+=", "-=", "*=", "&=", "|=", "^=", "<<=", ">>="])
                    return [f"{ind}{v} {op} {self.expr(INT, sc, 2)};"]
                return [f"{ind}{v} = {self.clipped(t, self.expr(t, sc, 3))};"]
        if c < 0.42:
            t = r.choice([INT, INT, BOOL, FLOAT, STR])
            return [f"{ind}print({self.expr(t, sc, 3)});"]
        if c < 0.50:
            a = self.pick([v for t in ARRAYS for v in self.vars(sc, t)])
            if a:
                at = next(s[a]["t"] for s in reversed(sc) if a in s)
                et = ELEM[at]
                return [f"{ind}{a}[{self.arr_index(a, sc, 2)}] = {self.clipped(et, self.expr(et, sc, 2))};"]
        if c < 0.54:
            v = self.pick(self.vars(sc, S, nn=True))
            if v:
                f, t = r.choice([("a", INT), ("f", FLOAT), ("s", STR), ("next", S)])
                if t == S:
                    return [f"{ind}{v}.next = {self.pick(self.vars(sc, S)) or 'null'};"]
                return [f"{ind}{v}.{f} = {self.clipped(t, self.expr(t, sc, 2))};"]
        if c < 0.59:
            v = self.fresh("s")
            nxt = self.pick(self.vars(sc, S)) or "null"
            line = (f"{ind}let {v} = S {{ a: {self.expr(INT, sc, 2)}, f: {self.expr(FLOAT, sc, 1)}, "
                    f"s: clip({self.expr(STR, sc, 1)}), next: {nxt} }};")
            self.declare(sc, v, S, False, True)
            return [line]
        if c < 0.61:
            v = self.fresh("n")
            init = self.pick(self.vars(sc, S)) or "null"
            self.declare(sc, v, S, True, False)
            return [f"{ind}let mut {v}: S = {init};"]
        if c < 0.64:
            v = self.pick(self.vars(sc, S, mut=True))
            if v:
                src = self.pick(self.vars(sc, S))
                nn = self.pick(self.vars(sc, S, nn=True))
                return [f"{ind}{v} = {r.choice([src or 'null', 'null', (nn + '.next') if nn else 'null'])};"]
        if c < 0.67:
            v = self.pick(self.vars(sc, S))
            if v:
                w, k = self.fresh("w"), self.fresh("k")
                return [f"{ind}let mut {w} = {v};", f"{ind}let mut {k} = 0;",
                        f"{ind}while {w} != null && {k} < 6 {{", f"{ind}    print({w}.a + {k});",
                        f"{ind}    {w} = {w}.next;", f"{ind}    {k} += 1;", f"{ind}}}"]
        if c < 0.70:
            v = self.pick(self.vars(sc, S, nn=False))
            if v and depth > 0:
                out = [f"{ind}if {v} != null {{"]
                sc.append({})
                inner = dict(next(s[v] for s in reversed(sc[:-1]) if v in s))
                if not inner["mut"]:
                    inner["nn"] = True
                sc[-1][v] = inner
                out += self.stmts(sc, depth - 1, inloop, ind + "    ")
                sc.pop()
                return out + [f"{ind}}}"]
        if c < 0.79 and depth > 0:
            out = [f"{ind}if {self.expr(BOOL, sc, 2)} {{"]
            out += self.block(sc, depth - 1, inloop, ind)
            if r.random() < 0.5:
                out += [f"{ind}}} else {{"]
                out += self.block(sc, depth - 1, inloop, ind)
            return out + [f"{ind}}}"]
        if c < 0.86 and depth > 0:
            i = self.fresh("i")
            lo = r.choice([0, 0, 0, 1, 2])
            out = [f"{ind}for {i} in {lo}..{lo + r.randint(1, 5)} {{"]
            out += self.block(sc, depth - 1, True, ind, {i: {"t": INT, "mut": False, "nn": False}})
            return out + [f"{ind}}}"]
        if c < 0.89 and depth > 0:
            k = self.fresh("c")
            out = [f"{ind}let mut {k} = 0;", f"{ind}while {k} < {r.randint(1, 4)} && {self.expr(BOOL, sc, 1)} {{",
                   f"{ind}    {k} += 1;"]
            out += self.block(sc, depth - 1, True, ind)
            return out + [f"{ind}}}"]
        if c < 0.92 and inloop:
            return [f"{ind}if {self.expr(BOOL, sc, 1)} {{ {r.choice(['break', 'continue'])}; }}"]
        if c < 0.94:
            a = self.pick(self.vars(sc, AI) + self.vars(sc, AY))
            if a:
                i = self.fresh("i")
                return [f"{ind}for {i} in 0..len({a}) {{ {a}[{i}] = {a}[{i}] + {self.expr(INT, sc, 1)}; }}"]
        if c < 0.945:  # loop shapes the range-based bounds-check elimination targets (and off-by-one near misses)
            a = self.pick(self.vars(sc, AI) + self.vars(sc, AY))
            if a:
                i = self.fresh("i")
                hi = "9" if self.trap_budget and r.random() < 0.15 else "8"
                op = "<=" if hi == "9" and r.random() < 0.5 else "<"
                if hi == "9":
                    self.trap_budget -= 1
                    hi = "8" if op == "<=" else "9"
                if r.random() < 0.5:
                    return [f"{ind}for {i} in 0..{hi} {{ {a}[{i}] = {a}[{i}] + {self.expr(INT, sc, 1)}; }}"]
                return [f"{ind}let mut {i} = 0;", f"{ind}while {i} {op} {hi} {{",
                        f"{ind}    {a}[{i}] = {a}[{i}] ^ {self.expr(INT, sc, 1)};", f"{ind}    {i} += 1;", f"{ind}}}"]
        if c < 0.95 and self.trap_budget:
            self.trap_budget -= 1
            return [r.choice([f"{ind}assert({self.expr(BOOL, sc, 1)});",
                              f"{ind}if {self.expr(BOOL, sc, 1)} {{ panic(\"boom \" + str({self.expr(INT, sc, 1)})); }}",
                              f"{ind}print(len(new [int]({self.expr(INT, sc, 1)} % 3 - 1)));"])]
        if c < 0.955:
            v = self.fresh("e")
            line = f"{ind}let {'mut ' if r.random() < 0.5 else ''}{v} = {self.enum_expr(sc, 2)};"
            self.declare(sc, v, "E", line.split()[1] == "mut", True)
            return [line]
        if c < 0.965:
            x, y, z = self.fresh("x"), self.fresh("y"), self.fresh("z")
            body1 = f"print({x} * 3);"
            return [f"{ind}match {self.enum_expr(sc, 2)} {{",
                    f"{ind}    A({x}) => {{ {body1} }}",
                    f"{ind}    B({y}, {z}) => {{ print({y}); print({z}); }}",
                    f"{ind}    C => {{ print(\"c\"); }}", f"{ind}}}"]
        if c < 0.97 and self.use_coll:
            return self.coll_stmt(sc, ind)
        if c < 0.99:
            cands = [f for f in self.funcs if f[2] is None]
            if cands:
                name, ps, _ = r.choice(cands)
                return [f"{ind}{name}({', '.join(self.arg(p, sc, 2) for p in ps)});"]
        return [f"{ind}print({self.expr(INT, sc, 2)} > 3);"]

    def coll_stmt(self, sc, ind):
        r = self.r
        k = r.randint(1, 12)
        c = r.random()
        if c < 0.25:
            b = self.fresh("sb")
            return [f"{ind}let {b} = strbuf.make();", f"{ind}for i in 0..{k} {{ strbuf.push({b}, clip({self.expr(STR, sc, 1)})); strbuf.push_int({b}, i); }}",
                    f"{ind}print(strbuf.to_str({b}));"]
        if c < 0.5:
            v = self.fresh("vec")
            return [f"{ind}let {v} = vec.make<int>();", f"{ind}for i in 0..{k} {{ vec.push({v}, {self.expr(INT, sc, 1)} + i); }}",
                    f"{ind}vec.reverse({v});", f"{ind}print(vec.sum({v}) + vec.get({v}, 0) + vec.index_of({v}, 3));"]
        if c < 0.75:
            m = self.fresh("map")
            return [f"{ind}let {m} = map.make();",
                    f"{ind}for i in 0..{k * 4} {{ map.put({m}, ({self.expr(INT, sc, 1)}) * i, i); }}",
                    f"{ind}print(map.size({m}) + map.get({m}, {self.expr(INT, sc, 1)}, 0 - 1));"]
        a = self.fresh("srt")
        return [f"{ind}let {a} = new [int]({k * 3});",
                f"{ind}for i in 0..len({a}) {{ {a}[i] = ({self.expr(INT, sc, 1)}) ^ (i * 7919); }}",
                f"{ind}sort.ints({a});", f"{ind}print(sort.is_sorted({a}));", f"{ind}print({a}[0] - {a}[len({a}) - 1]);"]

    # ---- whole program -----------------------------------------------------------------------------------------
    def func(self, idx):
        r = self.r
        ps = [r.choice([INT, INT, STR, FLOAT, BOOL, AI, S]) for _ in range(r.randint(1, 4))]
        ret = r.choice([INT, INT, STR, FLOAT, BOOL, None])
        name = f"f{idx}"
        sc = [{}]
        params = []
        for j, t in enumerate(ps):
            params.append(f"p{j}: {t}")
            self.declare(sc, f"p{j}", t)
        head = f"fn {name}({', '.join(params)})" + (f" -> {ret}" if ret else "") + " {"
        lines = [head]
        for _ in range(r.randint(0, 2)):
            at = r.choice(ARRAYS)
            a = self.fresh("a")
            lines.append(f"    let {a} = new [{NEWT[at]}](8);")
            self.declare(sc, a, at, False, True)
        lines += self.stmts(sc, 2, False, "    ")
        if ret:
            lines.append(f"    return {self.clipped(ret, self.expr(ret, sc, 3))};")
        lines.append("}")
        self.funcs.append((name, ps, ret))
        return lines

    def program(self):
        out = []
        if self.use_math:
            out.append('import "std/math.em" as math;')
        if self.use_str:
            out.append('import "std/str.em" as strings;')
        if self.use_coll:
            out += ['import "std/vec.em" as vec;', 'import "std/map.em" as map;', 'import "std/sort.em" as sort;',
                    'import "std/strbuf.em" as strbuf;']
        out.append(PRELUDE)
        for i in range(self.r.randint(1, 4)):
            out += self.func(i) + [""]
        sc = [{}]
        main = ["fn main() -> int {"]
        for _ in range(self.r.randint(0, 2)):
            at = self.r.choice(ARRAYS)
            a = self.fresh("a")
            main.append(f"    let {a} = new [{NEWT[at]}](8);")
            self.declare(sc, a, at, False, True)
        main += self.stmts(sc, 2, False, "    ")
        for name, ps, ret in self.funcs:
            call = f"{name}({', '.join(self.arg(p, sc, 2) for p in ps)})"
            main.append(f"    print({call});" if ret else f"    {call};")
        main += ["    return 3;", "}"]
        return "\n".join(out + main) + "\n"


# ---------------------------------------------------------------------------------------------- running
def run(cmd, timeout):
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout)
        return (p.returncode, p.stdout, p.stderr)
    except subprocess.TimeoutExpired:
        return ("timeout", b"", b"")


WASM = os.environ.get("EMBER_FUZZ_WASM", "1") != "0" and all(shutil.which(t) for t in ("node", "clang", "wasm-ld"))


def four_ways(src, tmp, timeout=20):
    """Runs the program on every path (interpreter, x86-64 and, when available, wasm; each at -O0 and -O1).
    Returns (status, results). status: 'ok', 'mismatch', 'compile-error', 'timeout'."""
    path = os.path.join(tmp, "p.em")
    with open(path, "w") as fh:
        fh.write(src)
    res = {}
    for lvl in ("-O0", "-O1"):
        res["interp" + lvl] = run([EMBC, path, "--interp", lvl], timeout)
        if lvl == "-O0" and res["interp-O0"][0] == 1 and b"error:" in res["interp-O0"][2] and b"runtime error" not in res["interp-O0"][2]:
            return "compile-error", res
        exe = os.path.join(tmp, "p" + lvl)
        c = subprocess.run([EMBC, path, lvl, "-o", exe], capture_output=True)
        res["native" + lvl] = ("compile-error", c.stdout, c.stderr) if c.returncode else run([exe], timeout)
        if WASM:
            js = exe + ".js"
            c = subprocess.run([EMBC, path, lvl, "--target", "wasm", "-o", js], capture_output=True)
            res["wasm" + lvl] = ("compile-error", c.stdout, c.stderr) if c.returncode else run(["node", js], timeout)
    vals = list(res.values())
    if any(v[0] == "timeout" for v in vals):
        return "timeout", res
    if any(v != vals[0] for v in vals):
        return "mismatch", res
    return "ok", res


def shrink(src, tmp, budget=400):
    """Delete statements and simplify expressions while the four paths still disagree."""
    tries = [0]

    def bad(s):
        tries[0] += 1
        return four_ways(s, tmp)[0] == "mismatch"

    lines = src.split("\n")
    changed = True
    while changed and tries[0] < budget:
        changed = False
        # 1. delete a statement or a whole block (a line opening a block through its matching close)
        i = len(lines) - 1
        while i >= 0 and tries[0] < budget:
            ln = lines[i].strip()
            if not ln or ln.startswith("}") or ln.startswith("struct") or ln.startswith("fn main"):
                i -= 1
                continue
            depth, j = 0, i
            while j < len(lines):
                depth += lines[j].count("{") - lines[j].count("}")
                if depth <= 0:
                    break
                j += 1
            cand = lines[:i] + lines[j + 1:]
            if bad("\n".join(cand)):
                lines = cand
                changed = True
            elif j > i and lines[i].rstrip().endswith("{") and lines[j].strip() == "}":
                cand = lines[:i] + lines[i + 1:j] + lines[j + 1:]  # unwrap: keep the body
                if bad("\n".join(cand)):
                    lines = cand
                    changed = True
            i = min(i, len(lines)) - 1
        # 2. replace parenthesised subexpressions with a literal or with one of their operands
        for i in range(len(lines)):
            k = 0
            while k < len(lines[i]) and tries[0] < budget:
                ln = lines[i]
                if ln[k] != "(":
                    k += 1
                    continue
                depth, e = 0, k
                while e < len(ln):
                    depth += (ln[e] == "(") - (ln[e] == ")")
                    if depth == 0:
                        break
                    e += 1
                inner = ln[k + 1:e]
                parts = [p for p in re.split(r" (?:[-+*/%&|^<>=!]+) ", inner) if p] if " " in inner else []
                done = False
                for rep in ["0", "1", "true", "0.0", '""'] + parts[:2]:
                    if rep == ln[k:e + 1]:
                        continue
                    cand_line = ln[:k] + rep + ln[e + 1:]
                    cand = lines[:i] + [cand_line] + lines[i + 1:]
                    if bad("\n".join(cand)):
                        lines = cand
                        changed = done = True
                        break
                if not done:
                    k += 1
    return "\n".join(lines)


def one(args):
    seed, do_shrink = args
    tmp = tempfile.mkdtemp(prefix="ember-fuzz-")
    src = Gen(seed).program()
    status, res = four_ways(src, tmp)
    report = None
    if status == "mismatch":
        os.makedirs(FAILDIR, exist_ok=True)
        with open(os.path.join(FAILDIR, f"seed{seed}.em"), "w") as fh:
            fh.write(src)
        small = shrink(src, tmp) if do_shrink else src
        with open(os.path.join(FAILDIR, f"seed{seed}.min.em"), "w") as fh:
            fh.write(small)
        status2, res2 = four_ways(small, tmp)
        report = "\n".join(f"   {k:12} -> exit {v[0]}, out {v[1][-80:]!r}, err {v[2][:100]!r}" for k, v in res2.items())
    elif status == "compile-error":
        report = res["interp-O0"][2].decode(errors="replace")[:400] + "\n" + src
    trapped = status == "ok" and res["interp-O0"][0] == 1
    return seed, status, trapped, report


def main():
    argv = sys.argv[1:]
    if argv and argv[0] == "--shrink":
        tmp = tempfile.mkdtemp(prefix="ember-fuzz-")
        print(shrink(open(argv[1]).read(), tmp, budget=2000))
        return
    jobs = os.cpu_count() or 2
    do_shrink = True
    if "--jobs" in argv:
        k = argv.index("--jobs")
        jobs = int(argv[k + 1])
        del argv[k:k + 2]
    if "--no-shrink" in argv:
        argv.remove("--no-shrink")
        do_shrink = False
    count = int(argv[0]) if argv else 100
    first = int(argv[1]) if len(argv) > 1 else 1
    stats = {"ok": 0, "mismatch": 0, "compile-error": 0, "timeout": 0}
    traps = 0
    with cf.ProcessPoolExecutor(jobs) as ex:
        for seed, status, trapped, report in ex.map(one, [(s, do_shrink) for s in range(first, first + count)]):
            stats[status] += 1
            traps += trapped
            if status == "mismatch":
                print(f"MISMATCH seed {seed}: saved tests/fuzz_failures/seed{seed}.em and seed{seed}.min.em\n{report}", flush=True)
            elif status == "compile-error":
                print(f"GENERATOR BUG seed {seed} (program does not compile):\n{report}", flush=True)
    print(f"{count} programs: {stats['ok']} agreed ({traps} of them ended in the same runtime trap on every path), "
          f"{stats['mismatch']} mismatches, {stats['timeout']} timeouts, {stats['compile-error']} generator bugs")
    sys.exit(1 if stats["mismatch"] or stats["compile-error"] else 0)


if __name__ == "__main__":
    main()

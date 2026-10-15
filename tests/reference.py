#!/usr/bin/env python3
"""Independent reference results for tests/programs/*.em.

Written in plain Python (with explicit 64-bit wrapping and C-style truncating division) so the expected
outputs do not depend on the Ember compiler being right. Run `python3 tests/reference.py` to regenerate
the .out files.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
MASK = (1 << 64) - 1


def wrap(x):
    x &= MASK
    return x - (1 << 64) if x >> 63 else x


def tdiv(a, b):
    q = abs(a) // abs(b)
    return wrap(-q if (a < 0) != (b < 0) else q)


def tmod(a, b):
    return wrap(a - tdiv(a, b) * b)


def b(x):
    return "true" if x else "false"


out = {}

# fib
f = lambda n: n if n < 2 else f(n - 1) + f(n - 2)
out["fib"] = "".join(f"{f(i)}\n" for i in range(15))

# sieve
n = 100000
s = bytearray(n + 1)
primes = [i for i in range(2, n + 1) if all(i % p for p in range(2, int(i ** 0.5) + 1))]
out["sieve"] = f"{len(primes)}\n{primes[-1]}\n"

# sort
seed, arr = 42, []
for _ in range(300):
    seed = (seed * 1103515245 + 12345) % 2147483648
    arr.append(seed % 1000)
arr.sort()
checksum = sum((i + 1) * v for i, v in enumerate(arr))
out["sort"] = "".join(f"{v}\n" for v in arr[:10]) + f"{arr[-1]}\ntrue\n{checksum}\n"


# collatz
def steps(x):
    c = 0
    while x != 1:
        x = x // 2 if x % 2 == 0 else 3 * x + 1
        c += 1
    return c


best = arg = 0
for i in range(1, 10000):
    st = steps(i)
    if st > best:
        best, arg = st, i
out["collatz"] = f"{arg}\n{best}\n"

# nqueens
import itertools


def queens(n):
    return sum(
        all(abs(p[i] - p[j]) != j - i for i in range(n) for j in range(i + 1, n))
        for p in itertools.permutations(range(n))
    )


out["nqueens"] = "".join(f"{queens(k)}\n" for k in range(4, 10))

# tail
sw = lambda a, bb, k: a * 1000 + bb if k % 2 == 0 else bb * 1000 + a
out["tail"] = f"{sum(range(50001))}\ndone\n{sw(1, 2, 5)}\n{sw(1, 2, 6)}\n"

# logic
out["logic"] = "a\nc\n2\ne\nf\n3\ng\nh\n4\ni\nj\nk\nl\ntrue\nfalse\n"

# arith
big = (1 << 63) - 1
m = -(1 << 63)
lines = [
    wrap(big + 1), wrap(big * 2), m, tdiv(m, -1), tmod(m, -1),
    tdiv(7, 2), tdiv(-7, 2), tdiv(7, -2), tmod(-7, 3), tmod(7, -3), tmod(-7, -3),
    wrap(-big), wrap(-m), tdiv(m, -1), tmod(m, -1),
    1 - 2 - 3, 2 + 3 * 4 - tdiv(10, 3), tmod(100, 7) * 3,
]
out["arith"] = "".join(f"{v}\n" for v in lines)

# io
out["io"] = "150\n"

# strings
out["strings"] = 'hello\tworld\nsay "hi"\nback\\slash\ntrue\nfalse\nember\nember\nember\n'

# breakcont
count = 0
for i in range(30):
    if i % 3 == 0:
        continue
    j = i + 1
    while True:
        if j >= 30:
            break
        if j % 5 == 0:
            j += 1
            continue
        if (i * j) % 7 == 0:
            count += 1
        j += 1
out["breakcont"] = f"{count}\n"

# safe_loop
out["safe_loop"] = f"{sum(i * i for i in range(1000))}\n1000\n"

# scopes
out["scopes"] = "11\n22\n11\n1\n300\n1\n"

# runtime-error programs: stdout printed before the failure
out["oob"] = "1\n"
out["neg_index"] = "1\n"
out["div0"] = "1\n"
out["neg_size"] = "1\n"

# floats
import math
g = 2.0 / 2.0
for _ in range(30):
    g = (g + 2.0 / g) / 2.0
nan = float("nan")
F = lambda v: f"{v:.6f}"
xs = [float(i) * 1.5 for i in range(4)]
acc = 0.0
for v in xs:
    acc = acc + v
out["floats"] = "".join(x + "\n" for x in [
    F(g), F(math.sqrt(2.0)), F(7 / 2.0), "3", "-3", F(-1.5 * 2.0), F(0.1 + 0.2), F(1.0e3 + 2.5e-1),
    "false", "true", "false", "false", "true", "true", "true", "false", "inf", "-9223372036854775808",
    "-9223372036854775808", F(acc), "4"])


def mandel(cr, ci, maxit):
    zr = zi = 0.0
    i = 0
    while i < maxit and zr * zr + zi * zi <= 4.0:
        t = zr * zr - zi * zi + cr
        zi = 2.0 * zr * zi + ci
        zr = t
        i += 1
    return i


tot = sum(mandel(x / 80.0 * 3.5 - 2.5, y / 40.0 * 2.0 - 1.0, 200) for y in range(40) for x in range(80))
out["mandelbrot"] = f"{tot}\n"

# structs: linked list, binary search tree, float struct arithmetic
class N:
    def __init__(self, k):
        self.k, self.l, self.r = k, None, None


def ins(t, k):
    if t is None:
        return N(k)
    if k < t.k:
        t.l = ins(t.l, k)
    else:
        t.r = ins(t.r, k)
    return t


def hgt(t):
    return 0 if t is None else max(hgt(t.l), hgt(t.r)) + 1


seed, root, keys = 7, None, []
for _ in range(200):
    seed = (seed * 1103515245 + 12345) % 2147483648
    keys.append(seed % 1000)
    root = ins(root, seed % 1000)
ks = sorted(keys)
out["structs"] = f"385\n100\n{hgt(root)}\n200\ntrue\n{ks[0]}\n{ks[-1]}\n1.750000\n3.000000\n3.500000\n"
out["null_deref"] = "1\n"
out["null_array"] = ""


# ---------------------------------------------------------------- standard library programs
def lines(*vals):
    r = []
    for v in vals:
        if isinstance(v, bool):
            r.append(b(v))
        elif isinstance(v, float):
            r.append(f"{v:.6f}")
        else:
            r.append(str(v))
    return "".join(x + "\n" for x in r)


# std_math
def ipow(base, e):
    r = 1
    for _ in range(e):
        r = wrap(r * base)
    return r


def is_prime(n):
    return n >= 2 and all(n % d for d in range(2, math.isqrt(n) + 1))


out["std_math"] = lines(7, 7, -1, 0, 3, 9, 10, 0, 5, (-7) % 5, 7 % 5, (-10) % 5, math.gcd(84, 36), math.gcd(-84, 36),
                        math.gcd(0, 5), 12, 0, 3 ** 13, 2 ** 62, 1, 0, math.isqrt(99), math.isqrt(100),
                        math.isqrt(1000000007 * 3), sum(1 for i in range(200) if is_prime(i)), 2.5, 1.5, 2.5,
                        3.141592653589793, 2.718281828459045 * 2.0)


# the 64-bit LCG in std/rand.em
class Rng:
    def __init__(self, seed):
        self.s = wrap(seed)

    def next(self):
        self.s = wrap(self.s * 6364136223846793005 + 1442695040888963407)
        return (self.s >> 33) & 0x7FFFFFFF  # arithmetic shift of a signed value, like Ember's >>

    def range(self, n):
        return self.next() % n  # next() is non-negative, so Python % equals C %


r = Rng(42)
res = [r.next() for _ in range(5)]
res.append(r.range(6))
res.append(r.next() / 2147483648.0)
a = list(range(10))
i = 9
while i > 0:
    j = r.range(i + 1)
    a[i], a[j] = a[j], a[i]
    i -= 1
res += a
counts = [0] * 6
for _ in range(6000):
    counts[r.range(6)] += 1
res += counts
out["std_rand"] = lines(*res)

# std_sort
r = Rng(12345)
a = sorted(r.range(1000) - 500 for _ in range(2000))
chk = 0
for v in a:
    chk = tmod(chk * 31 + v, 1000000007)  # C-style remainder: chk may go negative
fl = sorted(float(r.range(1000)) / 8.0 for _ in range(100))
words = sorted(["pear", "apple", "fig", "apple", "banana", ""])
out["std_sort"] = lines(True, a[0], a[1999], a[1000], chk, True, -1, True, 1, 500, True, fl[0], fl[99], fl[50], *words)

# std_vec
v = [i * i for i in range(20)]
res = [len(v), v[7]]
v[7] = -1
res.append(v[7])
res.append(v.pop())
res += [len(v), sum(v), v.index(16) if 16 in v else -1, v.index(17) if 17 in v else -1, 100 in v]
v.reverse()
res += [v[0], v[18], len(v), sum(v), 0]
sv = ["a", "bb"] + [str(i) for i in range(9)]
res += [len(sv), sv[1], sv[10], len(sv)]
out["std_vec"] = lines(*res)
out["std_vec_oob"] = "5\n"

# std_str
def count_nonoverlap(s, t):
    return s.count(t)  # Python's str.count is non-overlapping, scanning left to right


res = ["[" + "  Hello, World!  ".strip(" \t\n\r\v\f") + "]", "abc XyZ 09".upper(), "ABC XyZ 09".lower(),
       "stressed"[::-1], "ab" * 3, "[]", "hello world".find("o w"), "hello".find("xyz"), "aaa".find(""),
       "ell" in "hello", "hello".startswith("he"), "he".startswith("hello"), "hello".endswith("llo"),
       "hello".endswith(""), count_nonoverlap("abababa", "aba"), count_nonoverlap("aaaa", "aa"),
       "a-b-c".replace("-", "--"), "abc".replace("x", "y"), "aaa".replace("aa", "b"), "hello".replace("l", "")]
for parts in ["a,b,,c".split(","), "".split(","), "one::two::".split("::")]:
    res += ["[" + x + "]" for x in parts]
res += ["+".join("1 2 3".split(" ")), "[]", "42".rjust(5, "0"), "12345", True, False, True]
out["std_str"] = lines(*res)

# std_strbuf
sb = "".join(str(i) + "," for i in range(100))
big = "".join(str(i % 10) for i in range(20000))
out["std_strbuf"] = lines(len(sb), len(sb), sb[:20], ord(sb[-1]), "reset", len(big), big[19990:20000])

# std_map
keys = [i * 7919 - 3000 for i in range(1000)]
out["std_map"] = lines(1000, 5, -1, True, False, 99, 1000, 1000, min(keys), max(keys), True, 500)

# std_io (input: "3\n10 20 -30\nhello world\n\nlast line\n"); read_int stops right after "-30", so the first
# read_line returns the empty rest of that line
inp = open(os.path.join(HERE, "programs", "std_io.in")).read()
toks = inp.split()
rest = inp[inp.index("-30") + 3:]
ls = rest.split("\n")
if ls and ls[-1] == "":
    ls.pop()
out["std_io"] = lines(sum(int(t) for t in toks[1:4]), len(ls), *["[" + x + "]" for x in ls], True)

# lang_features
s = "hi" + ", " + "world"
out["lang_features"] = lines(s, len(s), ord(s[0]), s[4:9], "apple" < "banana", True, "42true1.500000", -17, "AB",
                             (0xFF & 0x0F) | (1 << 4) ^ 3, ~5, -16 >> 2, (((10 + 5) << 2) | 1), 300 & 0xFF, True,
                             True, "mid", 9, "mid", math.floor(2.7) * 1.0, 2.0 ** 10, 3.5)

# gc_churn: a list built by pushing at the head, so head is the last node created
out["gc_churn"] = lines(3000, sum(range(3000)), sum(len(f"{i}:{i * 7}") for i in range(3000)), f"2999:{2999 * 7}")

# multi_import
out["multi_import"] = lines(1 + 2 + 100, (10 + 20 + 100) * 2, 30, 100)

# precedence: bitwise binds tighter than comparison; & over ^ over |; shifts tighter than & but looser than +
res = []
for x in range(6):
    res.append((x & 1) == 1)
a_, b_, c_, d_ = 12, 10, 6, 3
res += [a_ | b_ ^ c_ & d_, a_ | (b_ ^ (c_ & d_)), (1 << 2 + 1), (1 << 2) + 1, 0xF0 >> 4 & 3, 5 & 3 < 2, 6 ^ 3 != 5,
        1 | 2 == 3, 7 & 4 > 0]
out["precedence"] = lines(*res)

# inline_rt: an inlined leaf function whose runtime calls use its parameters
out["inline_rt"] = lines(3, "abc", "xyzxyz", 5, True, "q-3")


# modules
out["modules"] = lines("one", "two", 41, 402, 5, 3 + 10 + 200, "one#5", 2.5, 7, 12, True, 1024, 7)
out["shadow_import"] = lines(-5, 6)
# std_compat: std_vec's program through the old names, plus two calls into other modules
out["std_compat"] = out["std_vec"] + lines(12, False)


# enums: shapes, an expression tree, tokens, enum fields in structs, identity equality
def area(s):
    k = s[0]
    return 3.0 * s[1] * s[1] if k == "c" else s[1] * s[2] if k == "r" else 0.0


def ev(e, x):
    k = e[0]
    if k == "num": return e[1]
    if k == "add": return wrap(ev(e[1], x) + ev(e[2], x))
    if k == "mul": return wrap(ev(e[1], x) * ev(e[2], x))
    if k == "neg": return wrap(-ev(e[1], x))
    return x


def sh(e):
    k = e[0]
    if k == "num": return str(e[1])
    if k == "add": return "(" + sh(e[1]) + " + " + sh(e[2]) + ")"
    if k == "mul": return sh(e[1]) + " * " + sh(e[2])
    if k == "neg": return "-" + sh(e[1])
    return e[1]


e = ("add", ("num", 2), ("mul", ("var", "x"), ("neg", ("num", 4))))
deep = ("num", 0)
for i in range(1, 200):
    deep = ("add", ("num", i), deep)
toks = [("w", "hi"), ("n", 42), ("n", -1), ("f", True), ("e",)]
def desc(t):
    if t[0] == "w": return "word " + t[1]
    if t[0] == "n": return "negative" if t[1] < 0 else "number " + str(t[1])
    return "other"
shapes = [("c", 1.0), ("r", 1.0, 2.0), ("e",)]
out["enums"] = lines(area(("c", 2.0)), area(("r", 2.0, 3.5)), area(("e",)), sh(e), ev(e, 5), ev(deep, 0),
                     *[desc(t) for t in toks], sum(area(x) for x in shapes), sum(1 for x in shapes if x[0] == "c"),
                     True, False, True, area(("c", 0.5)))

# generics
class BST:
    def __init__(self):
        self.root = None

    def insert(self, x):
        def ins(t):
            if t is None: return [None, x, None]
            if x < t[1]: return [ins(t[0]), t[1], t[2]]
            return [t[0], t[1], ins(t[2])]
        self.root = ins(self.root)


bst = BST()
keys = [50, 30, 70, 20, 40, 60, 80, 35, 45, 65]
for k in keys:
    bst.insert(k)
def tsize(t): return 0 if t is None else tsize(t[0]) + 1 + tsize(t[2])
def tdepth(t): return 0 if t is None else max(tdepth(t[0]), tdepth(t[2])) + 1
row3 = [3 * j for j in range(4)]
flags = [i % 3 == 0 for i in range(100)]
words = ["b", "a", "c"][::-1]
out["generics"] = lines(5, "five", 5.5, "one", 1, 2, 5, "4", 7, 4, 1, 3, 0, tsize(bst.root), tdepth(bst.root), len(keys),
                        min(keys), max(keys), len(row3), sum(row3), True in flags, flags.index(False), len(flags),
                        words[0] + words[2], "none", words[1], True, True)

out["generic_realloc"] = lines(42, "s", True)


# closures
def fibm(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a


out["closures"] = lines(5, -1, 42, 15, 2, 10, 10, 20, 3, 1, 207 + 1, 7, "hi!!", "x", 4 * 10 + 2 + 20,
                        "+ 13", "- 5", "max 9", fibm(80), True, False, True)
sq = [x * x for x in range(10)]
ev_ = [x for x in sq if x % 2 == 0]
people = [("ann", 31), ("bob", 17), ("cy", 45)]
adults = [p for p in people if p[1] >= 18]
out["std_iter"] = lines(sq[9], len(ev_), sum(ev_), any(x > 50 for x in sq), all(x < 50 for x in sq),
                        sum(1 for x in sq if x > 50), next(i for i, x in enumerate(sq) if x > 50),
                        "".join("w" + str(x) for x in range(10)), len("w9") / 2.0, len(adults),
                        "".join(p[0] + ";" for p in adults), sum(p[1] for p in people), 4, 3 + 9,
                        sum(1 for x in range(10) if x > 6), True, 0)

out["closure_opt"] = lines(5 + 7 + 3, 10, sum(x * x for x in range(1, 11)), "b", 1.5, 3)


# strength: x*2^k, x*3/5/9, x/2^k and x%2^k (C rounding) for edge values
sr = []
for x in [0, 1, -1, 7, -7, 8, -8, 9, -9, 1023, -1023, (1 << 63) - 1, -(1 << 63), 123456789, -123456789]:
    sr += [tdiv(x, 2), tmod(x, 2), tdiv(x, 8), tmod(x, 8), tdiv(x, 1024), tmod(x, 1024), wrap(x * 2), wrap(x * 8),
           wrap(x * 3), wrap(x * 5), wrap(x * 9), tdiv(x, 1 << 62), tmod(x, 1 << 62)]
out["strength"] = lines(*sr)


# GC torture tests
tot = 0
for rnd in range(400):
    tot += sum(rnd + i for i in range(500)) + 1
out["gc_cycles"] = lines(tot)
out["gc_deep"] = lines(2000000, sum(range(2000000)), 1000000, sum(len(str(i) + "x") for i in range(200000)))
out["gc_huge"] = lines(sum(r + (r * 3) % 256 + len(str(r)) + 10 for r in range(10)))
keep = [""] * 50000
tot = 0
for i in range(3000000):
    st = "k" + str(i % 977) + ":" + str(i)
    keep[i % 50000] = st
    tot += len(st)
out["gc_strings"] = lines(tot, sum(len(x) for x in keep), keep[12345])

out["files"] = lines(len("first line\nsecond line\n")) + "first line\nsecond line\n" + lines(True, False, True, False, "[]", True, 0, "exiting")
out["file_missing"] = "before\n"

# glibc prints the default NaN (sign bit set) as -nan; negating it gives nan; NaN arguments pass through
out["float_nan"] = "".join(x + "\n" for x in ["-nan", "-nan", "-nan", "-nan", "nan", "nan", "nan", "inf", "-inf", "inf", "-nan",
                                              "1.000000"])

for name, text in out.items():
    with open(os.path.join(HERE, "programs", name + ".out"), "w") as fh:
        fh.write(text)
print(f"wrote {len(out)} expected outputs")

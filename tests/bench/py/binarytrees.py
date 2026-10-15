# binary-trees in Python, the same shape as tests/bench/binarytrees.em
import sys
sys.setrecursionlimit(10000)
def make(d):
    return (None, None) if d == 0 else (make(d - 1), make(d - 1))
def check(n):
    return 1 if n[0] is None else 1 + check(n[0]) + check(n[1])
maxd = 16
print(check(make(maxd + 1)))
ll = make(maxd)
for d in range(4, maxd + 1, 2):
    iters = 1 << (maxd - d + 4)
    print(sum(check(make(d)) for _ in range(iters)))
print(check(ll))

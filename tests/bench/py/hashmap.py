# hashmap in Python (the built-in dict): 1,000,000 inserts, 2,000,000 lookups, as tests/bench/hashmap.em
n = 1000000
m = {}
for i in range(n): m[i * 7919 + 13] = i
hits = s = 0
for i in range(2 * n):
    v = m.get(i * 7919 + 13, -1)
    if v >= 0: hits += 1; s += v
print(len(m)); print(hits); print(s)

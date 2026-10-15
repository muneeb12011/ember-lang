# strbuild in Python: build the same 20 MB string as tests/bench/strbuild.em
s = "".join(str(i) + "," for i in range(3000000))
commas = s.count(",")
digits = sum(int(c) for c in s if c != ",")
print(len(s)); print(commas); print(digits)

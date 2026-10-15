.RECIPEPREFIX = >
CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra

SRCS := $(wildcard src/*.hpp) src/main.cpp

embc: $(SRCS) src/rt_data.hpp
> $(CXX) $(CXXFLAGS) src/main.cpp -o embc -pthread

# the runtime (C) and the standard library (Ember) are embedded into the compiler binary
src/rt_data.hpp: rt/ember_rt.c rt/ember_rt_wasm.c rt/ember_wasm.js $(wildcard std/*.em std/compat/*.em) tools/embed.py
> python3 tools/embed.py > $@

test: embc
> python3 tests/run_tests.py

stress: embc
> EMBER_GC_STRESS=1 python3 tests/run_tests.py --native-only

fuzz: embc
> python3 tests/fuzz.py 100

clean:
> rm -f embc a.out a.out.s

.PHONY: test stress fuzz clean

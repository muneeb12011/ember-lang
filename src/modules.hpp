// modules.hpp - one Unit per source file, and name resolution across files.
//
// Every top-level name (fn, struct, const) belongs to the file that declares it and is private unless marked `pub`.
// Names are made globally unique by mangling: the root file keeps its names as written; any other file gets a prefix
// derived from its file name ("std/math.em" -> "math.", so `abs` becomes `math.abs`). Mangling happens before type
// checking, so the IR, the optimizer and the code generator never see modules at all.
//
//   import "std/math.em" as math;   qualified access: math.abs(x), math.PI, math.Vec (pub names only)
//   import "util.em";               unqualified access to util's pub names
//
// An unqualified name is looked up in the file's own declarations, then the built-ins, then the files imported without
// `as` (ambiguity between two such files is an error that suggests `as`).
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "diag.hpp"

enum class SymK { Fn, Struct, Const };

inline const char* symKindName(SymK k) { return k == SymK::Fn ? "function" : k == SymK::Struct ? "struct" : "constant"; }

struct Sym {
    SymK k;
    std::string name, mangled;
    bool pub = false;
    Pos pos;
    int unit = 0;
    bool isEnum = false;  // SymK::Struct names both structs and enums (one namespace for types)
    int ntparams = 0;     // generic: number of type parameters
};

inline const char* symKindName(const Sym& s) { return s.k == SymK::Struct && s.isEnum ? "enum" : symKindName(s.k); }

struct Unit {
    std::string path;     // as shown in messages
    std::string prefix;   // "" for the root file, else e.g. "math."
    bool compat = false;  // a compatibility file for the old prefixed standard-library names
    std::map<std::pair<int, std::string>, Sym> syms;
    std::vector<int> globs;                // files imported without `as`
    std::map<std::string, int> aliases;    // `as` name -> unit
    std::map<std::string, Pos> aliasPos;
};

inline std::vector<Unit>& units() {
    static std::vector<Unit> u;
    return u;
}

inline std::string mangle(int unit, const std::string& n) { return units()[unit].prefix + n; }

inline bool isAlias(int unit, const std::string& n) {
    return unit >= 0 && unit < (int)units().size() && units()[unit].aliases.count(n);
}

// Declare a top-level name; returns false (and fills `err`) when the file already declares it.
inline bool declareSym(int unit, SymK k, const std::string& n, bool pub, Pos pos, CompileError& err, bool isEnum = false,
                       int ntparams = 0) {
    auto key = std::make_pair((int)k, n);
    auto& syms = units()[unit].syms;
    auto it = syms.find(key);
    if (it != syms.end()) {
        err = CompileError{pos, std::string(k == SymK::Struct ? "type" : symKindName(k)) + " `" + n + "` is defined twice"};
        err.notePos = it->second.pos;
        err.note = "the first definition is here";
        return false;
    }
    Sym s;
    s.k = k;
    s.name = n;
    s.mangled = mangle(unit, n);
    s.pub = pub;
    s.pos = pos;
    s.unit = unit;
    s.isEnum = isEnum;
    s.ntparams = ntparams;
    syms[key] = s;
    return true;
}

struct Resolved {
    const Sym* sym = nullptr;
    const Sym* hidden = nullptr;  // found, but private
    bool failed = false;  // an error was produced (found but not accessible, ambiguous, unknown member)
    CompileError err;
};

// q: the module alias written before the dot ("" for an unqualified name).
// Unqualified lookups search the file itself and its `as`-less imports; built-ins are checked by the caller in between.
inline Resolved resolveSym(int unit, const std::string& q, const std::string& n, SymK k, Pos at, bool includeOwn = true,
                           bool includeGlobs = true) {
    Resolved r;
    auto key = std::make_pair((int)k, n);
    auto privErr = [&](const Sym& s) {
        r.failed = true;
        r.hidden = &s;
        r.err = CompileError{at, std::string(symKindName(s)) + " `" + n + "` is private to " + units()[s.unit].path +
                                     "; mark it `pub` there to use it from another file"};
        r.err.notePos = s.pos;
        r.err.note = "`" + n + "` is declared here";
    };
    if (!q.empty()) {
        auto a = units()[unit].aliases.find(q);
        if (a == units()[unit].aliases.end()) {
            r.failed = true;
            r.err = CompileError{at, "unknown module `" + q + "`"};
            return r;
        }
        const Unit& u = units()[a->second];
        auto it = u.syms.find(key);
        if (it == u.syms.end()) {
            r.failed = true;
            std::string other;
            for (int kk = 0; kk < 3; kk++)
                if (kk != (int)k && u.syms.count({kk, n})) other = std::string(" (it has a ") + symKindName((SymK)kk) + " of that name)";
            r.err = CompileError{at, "module `" + q + "` (" + u.path + ") has no " + symKindName(k) + " `" + n + "`" + other};
            return r;
        }
        if (!it->second.pub) {
            privErr(it->second);
            return r;
        }
        r.sym = &it->second;
        return r;
    }
    const Unit& me = units()[unit];
    if (includeOwn) {
        auto own = me.syms.find(key);
        if (own != me.syms.end()) {
            r.sym = &own->second;
            return r;
        }
    }
    if (!includeGlobs) return r;
    const Sym* found = nullptr;
    const Sym* hidden = nullptr;
    for (int g : me.globs) {
        auto it = units()[g].syms.find(key);
        if (it == units()[g].syms.end()) continue;
        if (!it->second.pub) {
            hidden = &it->second;
            continue;
        }
        if (found && found->unit != it->second.unit) {
            r.failed = true;
            r.err = CompileError{at, "`" + n + "` is defined in both " + units()[found->unit].path + " and " +
                                         units()[it->second.unit].path + "; import one of them with `as` and qualify the name"};
            return r;
        }
        found = &it->second;
    }
    if (found) {
        r.sym = found;
        return r;
    }
    if (hidden) privErr(*hidden);
    return r;
}

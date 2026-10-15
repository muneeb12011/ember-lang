// sema.hpp - name resolution, type inference/checking, and the bounds-check proof.
//
// Reports every error it can find (not just the first). TyErr silences follow-on errors.
//
// Generic functions are checked once, with their type parameters as opaque types: a value of type T can be stored,
// passed, returned and compared with == / !=, nothing else, so a generic body that checks is valid for every type
// argument. At each call the type arguments are inferred (from the arguments, then from the type the context expects)
// and recorded in the call (Expr::targs); monomorphization (mono.hpp) later makes one copy per instance.
#pragma once
#include <map>
#include <set>

#include "ast.hpp"
#include "builtins.hpp"

class Sema {
    struct Sig {
        std::vector<Ty> params;
        Ty ret;
        Pos pos;
        std::vector<Ty> tparams;
    };
    struct Var {
        int id;
        Ty ty;
        bool mut;
        bool captured = false;  // a closure's copy of an outer variable
    };
    // the state of one function body being checked; anonymous functions nest, the enclosing ones wait in `outer`
    struct Ctx {
        std::vector<std::map<std::string, Var>> scopes;
        std::set<int> mutableIds, nonnullIds;
        int nextId = 0, loopDepth = 0;
        Ty curRet = TyUnit;
        std::map<std::string, Var> capScope;
        FnDecl* fn = nullptr;
    };
    std::vector<Ctx> outer;
    std::map<std::string, Var> capScope;  // variables the current anonymous function captured
    FnDecl* curFn = nullptr;              // the anonymous function being checked (nullptr: a top-level function)

    std::map<std::string, Sig> sigs;
    std::vector<std::map<std::string, Var>> scopes;
    std::set<int> mutableIds;  // variable ids declared `let mut`
    std::map<std::string, const ConstDecl*> consts;
    std::set<int> nonnullIds;  // immutable variables bound to a fresh allocation: provably never null
    std::vector<CompileError> errs, warns;
    std::set<std::string> warned;
    int curUnit = 0;  // the file the function being checked belongs to
    int nextId = 0, loopDepth = 0;
    Ty curRet = TyUnit;

    void err(Pos p, std::string m) { errs.push_back({p, std::move(m)}); }

    // a resolved top-level name; reports access errors and deprecated (compatibility-layer) names
    const Sym* resolved(const Expr& e, SymK k, bool* failed = nullptr) {
        Resolved r = resolveSym(curUnit, e.q, e.s, k, e.pos);
        if (failed) *failed = r.failed;
        if (r.failed) {
            errs.push_back(r.err);
            return nullptr;
        }
        if (r.sym && units()[r.sym->unit].compat && warned.insert(r.sym->mangled).second)
            warns.push_back({e.pos, "`" + e.s + "` is a deprecated name from before modules; import the module with `as` and use "
                                        "its new name (see " + units()[r.sym->unit].path + ")"});
        return r.sym;
    }
    static std::string shown(const Expr& e) { return e.q.empty() ? e.s : e.q + "." + e.s; }

    static Var* findIn(std::vector<std::map<std::string, Var>>& sc, std::map<std::string, Var>& caps, const std::string& n) {
        for (auto it = sc.rbegin(); it != sc.rend(); ++it) {
            auto f = it->find(n);
            if (f != it->end()) return &f->second;
        }
        auto c = caps.find(n);
        return c == caps.end() ? nullptr : &c->second;
    }
    // A name found in an enclosing function is captured: every anonymous function between there and here gets its own
    // copy (a new variable id, filled from the closure object on entry).
    Var* lookup(const std::string& n) {
        if (Var* v = findIn(scopes, capScope, n)) return v;
        for (int d = (int)outer.size() - 1; d >= 0; d--) {
            Var* v = findIn(outer[d].scopes, outer[d].capScope, n);
            if (!v) continue;
            Var cur = *v;
            for (size_t k = (size_t)d + 1; k <= outer.size(); k++) {
                bool here = k == outer.size();
                int& next = here ? nextId : outer[k].nextId;
                FnDecl* L = here ? curFn : outer[k].fn;
                Var nv{next++, cur.ty, false, true};
                L->caps.push_back({cur.id, nv.id, cur.ty, n});
                (here ? capScope : outer[k].capScope)[n] = nv;
                cur = nv;
            }
            return &capScope[n];
        }
        return nullptr;
    }

    int declare(Pos p, const std::string& n, Ty ty, bool mut) {
        auto& sc = scopes.back();
        if (sc.count(n)) err(p, "`" + n + "` is already declared in this scope");
        if (isAlias(curUnit, n)) err(p, "`" + n + "` is the name of an imported module (`import ... as " + n + "`); choose another name");
        int id = nextId++;
        sc[n] = Var{id, ty, mut};
        if (mut) mutableIds.insert(id);
        return id;
    }

    static bool assignable(Ty want, Ty got) { return want == got || (got == TyNull && isRef(want)); }

    // ---- generics -----------------------------------------------------------------------------------------
    // Bind the type parameters `ps` that occur in `decl` so that it matches `act`. Mismatches are not reported here:
    // the ordinary assignability check after substitution reports them with a better message.
    static void unify(Ty decl, Ty act, const std::vector<Ty>& ps, std::map<Ty, Ty>& b) {
        if (act == TyErr || act == TyNull || decl == TyErr || !validTy(decl) || !validTy(act)) return;
        if (std::find(ps.begin(), ps.end(), decl) != ps.end()) {
            if (!b.count(decl)) b[decl] = act;
            return;
        }
        if (isArr(decl) && isArr(act)) return unify(elemTy(decl), elemTy(act), ps, b);
        if (isFn(decl) && isFn(act) && fnParams(decl).size() == fnParams(act).size()) {
            const std::vector<Ty> dp = fnParams(decl), ap = fnParams(act);
            for (size_t i = 0; i < dp.size(); i++) unify(dp[i], ap[i], ps, b);
            return unify(fnRet(decl), fnRet(act), ps, b);
        }
        int sd = structId(decl), sa = structId(act);
        if (sd >= 0 && sa >= 0 && structs()[sd].templ >= 0 && structs()[sd].templ == structs()[sa].templ)
            for (size_t i = 0; i < structs()[sd].targs.size(); i++)
                unify(structs()[sd].targs[i], structs()[sa].targs[i], ps, b);
    }
    static bool mentions(Ty t, const std::vector<Ty>& ps) {
        if (!validTy(t)) return false;
        if (std::find(ps.begin(), ps.end(), t) != ps.end()) return true;
        if (isArr(t)) return mentions(elemTy(t), ps);
        if (isFn(t)) {
            for (Ty p : fnParams(t))
                if (mentions(p, ps)) return true;
            return mentions(fnRet(t), ps);
        }
        int sid = structId(t);
        if (sid >= 0)
            for (Ty a : structs()[sid].targs)
                if (mentions(a, ps)) return true;
        return false;
    }
    // an argument whose type depends on what the context expects: typed after the others
    static bool contextual(const Expr& e) {
        return e.k == EK::Null || (e.k == EK::ArrayLit && e.args.empty()) || (e.k == EK::EnumCtor && !e.hasTargs) ||
               (e.k == EK::Call && e.args.empty() && !e.hasTargs);
    }
    // Type the arguments of a generic call / literal. decl[i]: the declared type of argument i in terms of `ps`.
    // Informative arguments go first; then the expected result type; then the contextual ones, which can now be told
    // their type (as in `Tree.Node(Tree.Leaf, x, Tree.Leaf)`).
    void genericArgs(std::vector<ExprP>& args, const std::vector<Ty>& decl, const std::vector<Ty>& ps, std::map<Ty, Ty>& b,
                     std::vector<Ty>& at, Ty retDecl, Ty want) {
        at.assign(args.size(), TyErr);
        auto known = [&](size_t i) -> Ty {
            if (i >= decl.size() || decl[i] == TyErr) return TyErr;
            Ty sd = types().subst(decl[i], b);
            return mentions(sd, ps) ? TyErr : sd;
        };
        for (int pass = 0; pass < 2; pass++) {
            for (size_t i = 0; i < args.size(); i++) {
                if (contextual(*args[i]) != (pass == 1)) continue;
                at[i] = expr(*args[i], false, known(i));
                if (i < decl.size() && decl[i] != TyErr) unify(decl[i], at[i], ps, b);
            }
            if (pass == 0) unify(retDecl, want, ps, b);
        }
    }

    // the instance of template `idx` over its own parameters (Vec<T> inside Vec's own declaration)
    static Ty openSelf(int idx) { return types().instantiate(idx, structs()[idx].tparams); }

    // complete the bindings for `ps`; reports what cannot be inferred. Returns false on error.
    bool finishTargs(const std::vector<Ty>& ps, std::map<Ty, Ty>& b, std::vector<Ty>& out, Pos p, const std::string& what) {
        out.clear();
        bool ok = true;
        for (Ty t : ps) {
            auto it = b.find(t);
            if (it == b.end()) {
                err(p, "cannot infer the type argument `" + tyName(t) + "` of " + what + "; write the type arguments, as in `" +
                           what.substr(1, what.find('`', 1) - 1) + "<...>`, or give the result a declared type");
                ok = false;
                out.push_back(TyErr);
                continue;
            }
            if (it->second == TyByte) {
                err(p, "`byte` cannot be a type argument (it only exists as an array element type)");
                ok = false;
            }
            out.push_back(it->second);
        }
        return ok;
    }

    // ---- expressions --------------------------------------------------------------------------------------
    // `want` is the type the context expects (TyErr when it expects nothing in particular); it is only used to infer
    // type arguments that the operands leave open, as in `let v: Vec<int> = vec.make();`
    Ty expr(Expr& e, bool allowUnit = false, Ty want = TyErr) {
        Ty t = expr1(e, want);
        e.ty = t;
        if (t == TyUnit && !allowUnit) {
            err(e.pos, (e.k == EK::CallInd ? std::string("this function") : "`" + e.s + "`") + " returns no value, so it cannot be used as a value");
            e.ty = TyErr;
        }
        return e.ty;
    }

    Ty expr1(Expr& e, Ty want) {
        switch (e.k) {
            case EK::Int: return TyInt;
            case EK::Bool: return TyBool;
            case EK::Float: return TyFloat;
            case EK::Str: return TyStr;
            case EK::Var: {
                Var* v = e.q.empty() ? lookup(e.s) : nullptr;
                if (!v) {
                    if (e.q.empty() && isAlias(curUnit, e.s)) {
                        err(e.pos, "`" + e.s + "` is a module, not a value; write `" + e.s + ".name`");
                        return TyErr;
                    }
                    bool failed = false;
                    const Sym* cs = resolved(e, SymK::Const, &failed);
                    if (failed) return TyErr;
                    auto c = cs ? consts.find(cs->mangled) : consts.end();
                    if (c == consts.end() && !cs) {  // a top-level function used as a value
                        Resolved rf = resolveSym(curUnit, e.q, e.s, SymK::Fn, e.pos);
                        if (rf.failed) {
                            errs.push_back(rf.err);
                            return TyErr;
                        }
                        if (rf.sym) return fnRef(e, *rf.sym, want);
                        if (e.q.empty() && (isSpecialBuiltin(e.s) || findBuiltin(e.s))) {
                            err(e.pos, "the built-in `" + e.s + "` cannot be used as a value; wrap it in a function, as in `fn(x: float) -> float { return " +
                                           e.s + "(x); }`");
                            return TyErr;
                        }
                    }
                    if (c != consts.end()) {  // a constant is replaced by its literal right here
                        const ConstDecl& cd = *c->second;
                        e.s = cd.sval;
                        e.ival = cd.ival;
                        e.fval = cd.fval;
                        e.k = cd.ty == TyInt ? EK::Int : cd.ty == TyFloat ? EK::Float : cd.ty == TyBool ? EK::Bool : EK::Str;
                        return cd.ty;
                    }
                    err(e.pos, "unknown variable `" + e.s + "`");
                    return TyErr;
                }
                e.slot = v->id;
                return v->ty;
            }
            case EK::Unary: {
                Ty a = expr(*e.args[0]);
                if (a == TyErr) return TyErr;
                if (e.op == T::Tilde) {
                    if (a != TyInt) err(e.pos, std::string("`~` needs an int, found `") + tyName(a) + "`");
                    return TyInt;
                }
                if (e.op == T::Minus) {
                    if (a != TyInt && a != TyFloat) err(e.pos, std::string("`-` needs an int or float, found `") + tyName(a) + "`");
                    return a == TyFloat ? TyFloat : TyInt;
                }
                if (a != TyBool) err(e.pos, std::string("`!` needs a bool, found `") + tyName(a) + "`");
                return TyBool;
            }
            case EK::Binary: {
                Ty a = expr(*e.args[0]), b = expr(*e.args[1]);
                bool bad = a == TyErr || b == TyErr;
                std::string op = opText(e.op);
                auto mismatch = [&](const char* need) {
                    if (!bad)
                        err(e.pos, "`" + op + "` needs " + need + ", found `" + tyName(a) + "` and `" + tyName(b) + "`" +
                                       (((a == TyInt && b == TyFloat) || (a == TyFloat && b == TyInt))
                                            ? " (convert explicitly with float(x) or int(x))" : ""));
                };
                bool fl = a == TyFloat && b == TyFloat;
                bool ss = a == TyStr && b == TyStr;
                switch (e.op) {
                    case T::Plus:
                        if (fl) return TyFloat;
                        if (ss) return TyStr;
                        if (a != TyInt || b != TyInt) mismatch("two ints, two floats or two strs");
                        return TyInt;
                    case T::Minus: case T::Star: case T::Slash:
                        if (fl) return TyFloat;
                        if (a != TyInt || b != TyInt) mismatch("two ints or two floats");
                        return TyInt;
                    case T::Percent: case T::Amp: case T::Pipe: case T::Caret: case T::Shl: case T::Shr:
                        if (a != TyInt || b != TyInt) mismatch("two ints");
                        return TyInt;
                    case T::Lt: case T::Le: case T::Gt: case T::Ge:
                        if (!fl && !ss && (a != TyInt || b != TyInt)) mismatch("two ints, two floats or two strs");
                        return TyBool;
                    case T::Eq: case T::Ne:
                    {
                        // every type has ==: by value for scalars and strs, by identity for structs, enums and arrays;
                        // so a generic T may be compared too (its instance decides which)
                        bool ok = (a == b && (a == TyInt || a == TyBool || a == TyFloat || a == TyStr || isRef(a) || isParam(a))) ||
                                  (a == TyNull && (isRef(b) || b == TyNull)) || (b == TyNull && isRef(a));
                        if (!ok) mismatch("two values of the same type (or a reference and null)");
                        return TyBool;
                    }
                    default:  // && ||
                        if (a != TyBool || b != TyBool) mismatch("two bools");
                        return TyBool;
                }
            }
            case EK::Index: {
                Ty a = expr(*e.args[0]), b = expr(*e.args[1]);
                if (a != TyErr && !isArr(a) && a != TyStr) err(e.args[0]->pos, std::string("only arrays and strs can be indexed, found `") + tyName(a) + "`");
                if (b != TyErr && b != TyInt) err(e.args[1]->pos, std::string("index must be an int, found `") + tyName(b) + "`");
                if (a == TyStr || (e.args[0]->k == EK::Var && nonnullIds.count(e.args[0]->slot))) e.ival |= 2;  // a str is never null
                return isArr(a) ? valTy(elemTy(a)) : (a == TyStr ? TyInt : TyErr);
            }
            case EK::Null: return TyNull;
            case EK::Field: {
                Ty a = expr(*e.args[0]);
                if (a == TyErr) return TyErr;
                if (!isStruct(a)) {
                    err(e.pos, std::string("only structs have fields, found `") + tyName(a) + "`" +
                                   (isEnum(a) ? " (use `match` to look inside an enum value)" : ""));
                    return TyErr;
                }
                const StructInfo& si = structs()[structId(a)];
                for (size_t i = 0; i < si.fnames.size(); i++)
                    if (si.fnames[i] == e.s) {
                        e.slot = (int)i;
                        if (e.args[0]->k == EK::Var && nonnullIds.count(e.args[0]->slot)) e.ival |= 2;
                        return si.ftys[i];
                    }
                err(e.pos, "struct `" + si.name + "` has no field `" + e.s + "`");
                return TyErr;
            }
            case EK::StructLit: return structLit(e, want);
            case EK::EnumCtor: return enumCtor(e, want);
            case EK::Lambda: return lambda(e);
            case EK::CallInd: return callInd(e);
            case EK::FnRef: return e.ty;
            case EK::ArrayLit: {  // element type: the expected one, else the first element's
                Ty el = isArr(want) ? elemTy(want) : TyErr;
                std::vector<Ty> at;
                for (auto& a : e.args) {
                    at.push_back(expr(*a, false, el == TyByte ? TyInt : el));
                    if (el == TyErr && at.back() != TyErr && at.back() != TyNull) el = at.back();
                }
                if (el == TyErr) {
                    bool anyErr = false;
                    for (Ty t : at) anyErr |= t == TyErr;
                    if (!anyErr) err(e.pos, e.args.empty() ? "cannot infer the element type of `[]`; give it a declared type, as in `let a: [int] = [];`"
                                                           : "cannot infer the element type from `null`; give the array a declared type");
                    return TyErr;
                }
                for (size_t i = 0; i < at.size(); i++)
                    if (at[i] != TyErr && !assignable(valTy(el), at[i]))
                        err(e.args[i]->pos, "array element " + std::to_string(i + 1) + " should be `" + tyName(valTy(el)) + "`, found `" + tyName(at[i]) + "`");
                return arrayOf(el);
            }
            case EK::Call: return call(e, want);
        }
        return TyErr;
    }

    // `name` used as a value: a function value with no captures
    Ty fnRef(Expr& e, const Sym& fn, Ty want) {
        const Sig& sg = sigs.at(fn.mangled);
        e.k = EK::FnRef;
        e.s = fn.mangled;
        std::vector<Ty> params = sg.params;
        Ty ret = sg.ret;
        if (!sg.tparams.empty()) {
            std::map<Ty, Ty> b;
            if (e.hasTargs) {
                if (e.targs.size() != sg.tparams.size()) {
                    err(e.pos, "`" + fn.name + "` takes " + std::to_string(sg.tparams.size()) + " type argument(s), got " + std::to_string(e.targs.size()));
                    return TyErr;
                }
                for (size_t i = 0; i < sg.tparams.size(); i++) b[sg.tparams[i]] = e.targs[i];
            }
            unify(fnType(sg.params, sg.ret), want, sg.tparams, b);
            if (!finishTargs(sg.tparams, b, e.targs, e.pos, "`" + fn.name + "`")) return TyErr;
            std::map<Ty, Ty> m;
            for (size_t i = 0; i < sg.tparams.size(); i++) m[sg.tparams[i]] = e.targs[i];
            for (Ty& p : params) p = types().subst(p, m);
            ret = types().subst(ret, m);
        } else if (e.hasTargs) {
            err(e.pos, "`" + fn.name + "` is not generic, so it takes no type arguments");
        }
        if (params.size() > 5) err(e.pos, "a function used as a value takes at most 5 parameters");
        return fnType(params, ret);
    }

    // fn(x: int) -> int { ... }: checked as a function of its own; outer variables it uses become captures
    Ty lambda(Expr& e) {
        FnDecl& f = *e.fn;
        Ctx saved;
        saved.scopes = std::move(scopes);
        saved.mutableIds = std::move(mutableIds);
        saved.nonnullIds = std::move(nonnullIds);
        saved.nextId = nextId;
        saved.loopDepth = loopDepth;
        saved.curRet = curRet;
        saved.capScope = std::move(capScope);
        saved.fn = curFn;
        outer.push_back(std::move(saved));
        scopes.clear();
        scopes.emplace_back();
        mutableIds.clear();
        nonnullIds.clear();
        capScope.clear();
        nextId = 0;
        loopDepth = 0;
        curRet = f.ret;
        curFn = &f;
        f.caps.clear();
        std::vector<Ty> ps;
        for (auto& pr : f.params) {
            declare(pr.pos, pr.name, pr.ty, false);
            ps.push_back(pr.ty);
        }
        f.cloId = nextId++;
        if (f.params.size() > 5) err(f.pos, "an anonymous function takes at most 5 parameters");
        block(f.body);
        if (f.ret != TyUnit && !alwaysReturns(f.body))
            err(f.pos, "this anonymous function must return `" + tyName(f.ret) + "` on every path");
        f.nvars = nextId;
        Ctx& o = outer.back();
        scopes = std::move(o.scopes);
        mutableIds = std::move(o.mutableIds);
        nonnullIds = std::move(o.nonnullIds);
        nextId = o.nextId;
        loopDepth = o.loopDepth;
        curRet = o.curRet;
        capScope = std::move(o.capScope);
        curFn = o.fn;
        outer.pop_back();
        return fnType(ps, f.ret);
    }

    Ty callInd(Expr& e) {
        Ty ft = expr(*e.args[0]);
        if (ft != TyErr && !isFn(ft)) {
            err(e.args[0]->pos, "only functions can be called; this is a `" + tyName(ft) + "`");
            ft = TyErr;
        }
        if (ft == TyErr) {
            for (size_t i = 1; i < e.args.size(); i++) expr(*e.args[i]);
            return TyErr;
        }
        const std::vector<Ty> ps = fnParams(ft);
        std::vector<Ty> at;
        for (size_t i = 1; i < e.args.size(); i++) at.push_back(expr(*e.args[i], false, i - 1 < ps.size() ? ps[i - 1] : TyErr));
        if (at.size() != ps.size()) {
            err(e.pos, "this `" + tyName(ft) + "` takes " + std::to_string(ps.size()) + " argument(s), got " + std::to_string(at.size()));
        } else {
            for (size_t i = 0; i < at.size(); i++)
                if (at[i] != TyErr && !assignable(ps[i], at[i]))
                    err(e.args[i + 1]->pos, "argument " + std::to_string(i + 1) + " should be `" + tyName(ps[i]) + "`, found `" + tyName(at[i]) + "`");
        }
        if (ps.size() > 5) err(e.pos, "a function called through a value takes at most 5 parameters");
        e.ival = e.args[0]->k == EK::Var && nonnullIds.count(e.args[0]->slot) ? 2 : 0;
        return fnRet(ft);
    }

    Ty structLit(Expr& e, Ty want) {
        int id = structIndex(e.s);
        if (id < 0) {
            Resolved rs = resolveSym(curUnit, e.q, e.s, SymK::Struct, e.pos);
            if (rs.failed) {
                errs.push_back(rs.err);
                for (auto& a : e.args) expr(*a);
                return TyErr;
            }
            id = rs.sym ? structIndex(rs.sym->mangled) : -1;
        }
        if (id < 0) {
            for (auto& a : e.args) expr(*a);
            err(e.pos, "unknown struct `" + shown(e) + "`");
            return TyErr;
        }
        Ty inst = structs()[id].ty;
        std::vector<Ty> at;
        if (structs()[id].isTemplate()) {
            const std::vector<Ty> ps = structs()[id].tparams;
            std::map<Ty, Ty> b;
            if (e.hasTargs) {
                for (size_t i = 0; i < ps.size(); i++) b[ps[i]] = e.targs[i];
            }
            const std::vector<std::string> fn = structs()[id].fnames;
            const std::vector<Ty> ft = structs()[id].ftys;
            std::vector<Ty> decl(e.args.size(), TyErr);
            for (size_t i = 0; i < e.args.size(); i++)
                for (size_t f = 0; f < fn.size(); f++)
                    if (fn[f] == e.names[i]) decl[i] = ft[f];
            genericArgs(e.args, decl, ps, b, at, openSelf(id), want);
            if (!finishTargs(ps, b, e.targs, e.pos, "`" + structs()[id].local + "`")) return TyErr;
            inst = types().instantiate(id, e.targs);
            id = structId(inst);
        } else {
            for (size_t i = 0; i < e.args.size(); i++) {
                Ty fw = TyErr;
                for (size_t f = 0; f < structs()[id].fnames.size(); f++)
                    if (structs()[id].fnames[f] == e.names[i]) fw = structs()[id].ftys[f];
                at.push_back(expr(*e.args[i], false, fw));
            }
        }
        const StructInfo& si = structs()[id];
        std::vector<char> seen(si.fnames.size(), 0);
        e.idx.assign(e.args.size(), -1);
        for (size_t i = 0; i < e.names.size(); i++) {
            size_t f = 0;
            while (f < si.fnames.size() && si.fnames[f] != e.names[i]) f++;
            if (f == si.fnames.size()) {
                err(e.args[i]->pos, "struct `" + si.name + "` has no field `" + e.names[i] + "`");
                continue;
            }
            if (seen[f]) {
                err(e.args[i]->pos, "field `" + e.names[i] + "` is given twice");
                continue;
            }
            seen[f] = 1;
            e.idx[i] = (int)f;
            if (at[i] != TyErr && !assignable(si.ftys[f], at[i]))
                err(e.args[i]->pos, "field `" + e.names[i] + "` should be `" + tyName(si.ftys[f]) + "`, found `" + tyName(at[i]) + "`");
        }
        for (size_t f = 0; f < seen.size(); f++)
            if (!seen[f]) err(e.pos, "missing field `" + si.fnames[f] + "` in `" + si.name + "` literal");
        e.s = si.name;
        return inst;
    }

    Ty enumCtor(Expr& e, Ty want) {
        int id = structIndex(e.s);
        const std::string& vn = e.names[0];
        int v = -1;
        for (size_t k = 0; k < structs()[id].vnames.size(); k++)
            if (structs()[id].vnames[k] == vn) v = (int)k;
        if (v < 0) {
            for (auto& a : e.args) expr(*a);
            std::string list;
            for (auto& n : structs()[id].vnames) list += (list.empty() ? "" : ", ") + n;
            err(e.pos, "enum `" + structs()[id].local + "` has no variant `" + vn + "` (its variants: " + list + ")");
            return TyErr;
        }
        Ty inst = structs()[id].ty;
        std::vector<Ty> at;
        if (structs()[id].isTemplate()) {
            const std::vector<Ty> ps = structs()[id].tparams;
            std::map<Ty, Ty> b;
            if (e.hasTargs)
                for (size_t i = 0; i < ps.size(); i++) b[ps[i]] = e.targs[i];
            const std::vector<Ty> pt = structs()[id].vtys[v];
            genericArgs(e.args, pt, ps, b, at, openSelf(id), want);
            if (!finishTargs(ps, b, e.targs, e.pos, "`" + structs()[id].local + "." + vn + "`")) return TyErr;
            inst = types().instantiate(id, e.targs);
        } else {
            for (size_t i = 0; i < e.args.size(); i++)
                at.push_back(expr(*e.args[i], false, i < structs()[id].vtys[v].size() ? structs()[id].vtys[v][i] : TyErr));
        }
        const StructDef& d = structs()[structId(inst)];
        const std::vector<Ty>& pay = d.vtys[v];
        if (at.size() != pay.size()) {
            err(e.pos, "`" + structs()[id].local + "." + vn + "` carries " + std::to_string(pay.size()) + " value(s), got " +
                           std::to_string(at.size()));
        } else {
            for (size_t i = 0; i < at.size(); i++)
                if (at[i] != TyErr && !assignable(pay[i], at[i]))
                    err(e.args[i]->pos, "value " + std::to_string(i + 1) + " of `" + structs()[id].local + "." + vn + "` should be `" +
                                            tyName(pay[i]) + "`, found `" + tyName(at[i]) + "`");
        }
        e.slot = v;
        e.s = d.name;
        return inst;
    }

    Ty call(Expr& e, Ty want) {
        bool plain = e.q.empty();
        if (plain) {
            if (Var* v = lookup(e.s)) {  // a local variable holding a function: call through it
                if (!isFn(v->ty)) {
                    err(e.pos, "`" + e.s + "` is a `" + tyName(v->ty) + "`, not a function");
                    for (auto& a : e.args) expr(*a);
                    return TyErr;
                }
                auto callee = std::make_unique<Expr>();
                callee->k = EK::Var;
                callee->pos = e.pos;
                callee->s = e.s;
                e.args.insert(e.args.begin(), std::move(callee));
                e.k = EK::CallInd;
                return callInd(e);
            }
        }
        const Sym* user = nullptr;
        if (!plain || !isSpecialBuiltin(e.s)) {
            Resolved own = resolveSym(curUnit, e.q, e.s, SymK::Fn, e.pos, true, false);
            if (own.failed) {
                errs.push_back(own.err);
                for (auto& a : e.args) expr(*a);
                return TyErr;
            }
            user = own.sym;
            if (!user && !(plain && findBuiltin(e.s))) {  // the file's own names, then built-ins, then `as`-less imports
                bool failed = false;
                user = resolved(e, SymK::Fn, &failed);
                if (failed) {
                    for (auto& a : e.args) expr(*a);
                    return TyErr;
                }
            }
        }
        if (user) return userCall(e, *user, want);
        if (!plain) {
            for (auto& a : e.args) expr(*a);
            err(e.pos, "unknown function `" + shown(e) + "`");
            return TyErr;
        }
        if (e.hasTargs) err(e.pos, "`" + e.s + "` is not generic, so it takes no type arguments");
        std::vector<Ty> at;
        for (auto& a : e.args) at.push_back(expr(*a));
        bool anyErr = false;
        for (Ty t : at) anyErr |= t == TyErr;
        auto arity = [&](size_t n) {
            if (e.args.size() != n) {
                err(e.pos, "`" + e.s + "` takes " + std::to_string(n) + " argument(s), got " + std::to_string(e.args.size()));
                return false;
            }
            return true;
        };
        if (e.s == "print") {
            if (arity(1) && !anyErr && at[0] != TyInt && at[0] != TyFloat && at[0] != TyBool && at[0] != TyStr)
                err(e.args[0]->pos, std::string("`print` takes an int, float, bool or str, found `") + tyName(at[0]) + "`");
            return TyUnit;
        }
        if (e.s == "len") {
            if (arity(1) && !anyErr && !isArr(at[0]) && at[0] != TyStr) err(e.args[0]->pos, std::string("`len` takes an array or a str, found `") + tyName(at[0]) + "`");
            if (e.args.size() == 1 && ((e.args[0]->k == EK::Var && nonnullIds.count(e.args[0]->slot)) || at[0] == TyStr)) e.ival |= 2;
            return TyInt;
        }
        if (e.s == "new_array") {
            if (arity(1) && !anyErr && at[0] != TyInt) err(e.args[0]->pos, std::string("`new_array` takes an int size, found `") + tyName(at[0]) + "`");
            return TyIntArr;
        }
        if (e.s == "new_floats") {
            if (arity(1) && !anyErr && at[0] != TyInt) err(e.args[0]->pos, std::string("`new_floats` takes an int size, found `") + tyName(at[0]) + "`");
            return TyFloatArr;
        }
        if (e.s == "float" || e.s == "int" || e.s == "sqrt") {
            Ty need = e.s == "float" ? TyInt : TyFloat;
            if (arity(1) && !anyErr && at[0] != need)
                err(e.args[0]->pos, "`" + e.s + "` takes a `" + tyName(need) + "`, found `" + tyName(at[0]) + "`");
            return e.s == "int" ? TyInt : TyFloat;
        }
        if (e.s == "read_int") {
            arity(0);
            return TyInt;
        }
        if (e.s == "new") {
            if (arity(1) && !anyErr && at[0] != TyInt) err(e.args[0]->pos, std::string("array size must be an int, found `") + tyName(at[0]) + "`");
            return (Ty)e.slot;
        }
        if (e.s == "str") {
            if (arity(1) && !anyErr && at[0] != TyInt && at[0] != TyFloat && at[0] != TyBool && at[0] != TyStr)
                err(e.args[0]->pos, std::string("`str` takes an int, float, bool or str, found `") + tyName(at[0]) + "`");
            return TyStr;
        }
        if (e.s == "assert") {
            if (arity(1) && !anyErr && at[0] != TyBool) err(e.args[0]->pos, std::string("`assert` takes a bool, found `") + tyName(at[0]) + "`");
            return TyUnit;
        }
        if (const Builtin* bi = findBuiltin(e.s)) {
            if (e.args.size() != bi->params.size()) {
                err(e.pos, "`" + e.s + "` takes " + std::to_string(bi->params.size()) + " argument(s), got " + std::to_string(e.args.size()));
            } else {
                for (size_t i = 0; i < at.size(); i++)
                    if (at[i] != TyErr && !assignable(bi->params[i], at[i]))
                        err(e.args[i]->pos, "argument " + std::to_string(i + 1) + " of `" + e.s + "` should be `" + tyName(bi->params[i]) + "`, found `" + tyName(at[i]) + "`");
            }
            return bi->ret;
        }
        err(e.pos, "unknown function `" + e.s + "`");
        return TyErr;
    }

    Ty userCall(Expr& e, const Sym& fn, Ty want) {
        std::string nm = shown(e);
        e.s = fn.mangled;
        auto it = sigs.find(e.s);
        if (it == sigs.end()) {
            for (auto& a : e.args) expr(*a);
            return TyErr;
        }
        const Sig sg = it->second;
        std::vector<Ty> params = sg.params;
        Ty ret = sg.ret;
        std::vector<Ty> at;
        if (!sg.tparams.empty()) {
            std::map<Ty, Ty> b;
            if (e.hasTargs) {
                if (e.targs.size() != sg.tparams.size()) {
                    err(e.pos, "`" + nm + "` takes " + std::to_string(sg.tparams.size()) + " type argument(s), got " + std::to_string(e.targs.size()));
                    for (auto& a : e.args) expr(*a);
                    return TyErr;
                }
                for (size_t i = 0; i < sg.tparams.size(); i++) b[sg.tparams[i]] = e.targs[i];
            }
            genericArgs(e.args, params, sg.tparams, b, at, ret, want);
            if (!finishTargs(sg.tparams, b, e.targs, e.pos, "`" + nm + "`")) return TyErr;
            std::map<Ty, Ty> m;
            for (size_t i = 0; i < sg.tparams.size(); i++) m[sg.tparams[i]] = e.targs[i];
            for (Ty& p : params) p = types().subst(p, m);
            ret = types().subst(ret, m);
        } else {
            if (e.hasTargs) err(e.pos, "`" + nm + "` is not generic, so it takes no type arguments");
            for (size_t i = 0; i < e.args.size(); i++) at.push_back(expr(*e.args[i], false, i < params.size() ? params[i] : TyErr));
        }
        if (e.args.size() != params.size()) {
            err(e.pos, "`" + nm + "` takes " + std::to_string(params.size()) + " argument(s), got " + std::to_string(e.args.size()));
        } else {
            for (size_t i = 0; i < at.size(); i++)
                if (at[i] != TyErr && !assignable(params[i], at[i]))
                    err(e.args[i]->pos, "argument " + std::to_string(i + 1) + " of `" + nm + "` should be `" + tyName(params[i]) + "`, found `" + tyName(at[i]) + "`");
        }
        return ret;
    }

    void block(std::vector<StmtP>& b) {
        scopes.emplace_back();
        for (auto& s : b) stmt(*s);
        scopes.pop_back();
    }

    // ---- bounds-check proof -------------------------------------------------
    // Inside `for i in <literal >= 0> .. len(a)` where `a` is an immutable variable, `a[i]` can never be
    // out of range: `i` is read-only, starts at >= 0, and stays below the fixed length of `a`.
    void markSafe(Expr& e, int arr, int ivar) {
        if (e.k == EK::Lambda) return;  // a different function: its variable ids mean other things
        if (e.k == EK::Index && e.args[0]->k == EK::Var && e.args[0]->slot == arr && e.args[1]->k == EK::Var &&
            e.args[1]->slot == ivar)
            e.ival |= 3;  // in range, and non-null (len(a) already ran on it)
        for (auto& a : e.args) markSafe(*a, arr, ivar);
    }
    void markSafe(std::vector<StmtP>& b, int arr, int ivar) {
        for (auto& s : b) {
            if (s->e1) markSafe(*s->e1, arr, ivar);
            if (s->e2) markSafe(*s->e2, arr, ivar);
            markSafe(s->body, arr, ivar);
            markSafe(s->els, arr, ivar);
            for (auto& a : s->arms) markSafe(a.body, arr, ivar);
        }
    }

    void matchStmt(Stmt& s) {
        Ty t = expr(*s.e1);
        if (t != TyErr && !isEnum(t)) err(s.e1->pos, std::string("`match` needs an enum value, found `") + tyName(t) + "`");
        const StructDef* d = isEnum(t) ? &structs()[structId(t)] : nullptr;
        std::vector<char> covered(d ? d->vnames.size() : 0, 0);
        bool wild = false;
        for (MatchArm& a : s.arms) {
            std::vector<Ty> pay;
            if (a.variant == "_") {
                if (!a.binds.empty()) err(a.pos, "the `_` arm matches anything and binds nothing");
                if (wild) err(a.pos, "a second `_` arm can never run");
                wild = true;
            } else if (d) {
                int v = -1;
                for (size_t k = 0; k < d->vnames.size(); k++)
                    if (d->vnames[k] == a.variant) v = (int)k;
                if (v < 0) {
                    err(a.pos, "enum `" + d->local + "` has no variant `" + a.variant + "`");
                } else {
                    if (covered[v]) err(a.pos, "variant `" + a.variant + "` is matched twice");
                    covered[v] = 1;
                    a.vidx = v;
                    pay = d->vtys[v];
                    if (a.binds.size() != pay.size()) {
                        std::string ex = a.variant;
                        if (!pay.empty()) {
                            ex += "(";
                            for (size_t k = 0; k < pay.size(); k++) ex += (k ? ", " : "") + std::string(1, (char)('a' + k));
                            ex += ")";
                        }
                        err(a.pos, "variant `" + a.variant + "` carries " + std::to_string(pay.size()) + " value(s), so the arm is written `" +
                                       ex + " => ...`" + (pay.empty() ? "" : " (use `_` for a value you do not need)"));
                    }
                }
            }
            scopes.emplace_back();
            a.slots.assign(a.binds.size(), -1);
            for (size_t k = 0; k < a.binds.size(); k++)
                if (a.binds[k] != "_") a.slots[k] = declare(a.bpos[k], a.binds[k], k < pay.size() ? pay[k] : TyErr, false);
            block(a.body);
            scopes.pop_back();
        }
        s.mut = wild;  // Match: `mut` records exhaustiveness for alwaysReturns
        if (d && !wild) {
            std::string miss;
            int nmiss = 0;
            for (size_t k = 0; k < covered.size(); k++)
                if (!covered[k]) { miss += (miss.empty() ? "`" : ", `") + d->vnames[k] + "`"; nmiss++; }
            if (nmiss)
                err(s.pos, "this `match` does not cover every variant of `" + d->local + "`: missing " + miss +
                               " (add an arm for " + (nmiss == 1 ? "it" : "each") + ", or a `_ => { ... }` arm)");
            else s.mut = true;
        }
    }

    void stmt(Stmt& s) {
        switch (s.k) {
            case SK::Let: {
                Ty t = expr(*s.e1, false, s.hasTy ? s.declTy : TyErr);
                if (t == TyNull && !s.hasTy) err(s.e1->pos, "cannot infer a type from `null`; write `let name: Type = null;`");
                if (s.hasTy && t != TyErr && !assignable(s.declTy, t))
                    err(s.e1->pos, std::string("expected `") + tyName(s.declTy) + "`, found `" + tyName(t) + "`");
                Ty vt = s.hasTy ? s.declTy : t;
                s.declTy = vt;
                s.slot = declare(s.pos, s.name, vt, s.mut);
                if (!s.mut && (s.e1->k == EK::StructLit || s.e1->k == EK::EnumCtor || s.e1->k == EK::ArrayLit || s.e1->k == EK::Lambda ||
                               s.e1->k == EK::FnRef ||
                               (s.e1->k == EK::Call && (s.e1->s == "new_array" || s.e1->s == "new_floats" || s.e1->s == "new"))))
                    nonnullIds.insert(s.slot);
                break;
            }
            case SK::Assign: {
                Expr& tg = *s.e1;
                if (tg.k == EK::Var && (!tg.q.empty() || !lookup(tg.s)) && resolveSym(curUnit, tg.q, tg.s, SymK::Const, tg.pos).sym) {
                    err(tg.pos, "cannot assign to the constant `" + tg.s + "`");
                    expr(*s.e2);
                    break;
                }
                Ty tt = expr(tg);
                if (tg.k == EK::Index && tg.args[0]->ty == TyStr) err(tg.pos, "strs are immutable; build a new one instead (or use a [byte] buffer)");
                if (tg.k == EK::Var) {
                    Var* v = lookup(tg.s);
                    if (v && v->captured)
                        err(tg.pos, "`" + tg.s + "` is captured by this anonymous function, and closures capture variables by value "
                                    "(a copy made when the closure is created), so assigning to it would only change the copy; "
                                    "to share mutable state, keep it in a struct field or an array element");
                    else if (v && !v->mut)
                        err(tg.pos, "cannot assign to `" + tg.s + "` because it is not mutable; declare it with `let mut " + tg.s + "`");
                }
                Ty vt = expr(*s.e2, false, tt);
                if (tt != TyErr && vt != TyErr && !assignable(tt, vt))
                    err(s.e2->pos, std::string("cannot assign `") + tyName(vt) + "` to a `" + tyName(tt) + "`");
                break;
            }
            case SK::If: {
                Ty c = expr(*s.e1);
                if (c != TyErr && c != TyBool) err(s.e1->pos, std::string("`if` condition must be a bool, found `") + tyName(c) + "`");
                block(s.body);
                block(s.els);
                break;
            }
            case SK::While: {
                Ty c = expr(*s.e1);
                if (c != TyErr && c != TyBool) err(s.e1->pos, std::string("`while` condition must be a bool, found `") + tyName(c) + "`");
                loopDepth++;
                block(s.body);
                loopDepth--;
                break;
            }
            case SK::For: {
                Ty a = expr(*s.e1), b = expr(*s.e2);
                if (a != TyErr && a != TyInt) err(s.e1->pos, std::string("range start must be an int, found `") + tyName(a) + "`");
                if (b != TyErr && b != TyInt) err(s.e2->pos, std::string("range end must be an int, found `") + tyName(b) + "`");
                scopes.emplace_back();
                s.slot = declare(s.pos, s.name, TyInt, false);
                loopDepth++;
                block(s.body);
                loopDepth--;
                scopes.pop_back();
                const Expr& hi = *s.e2;
                if (s.e1->k == EK::Int && s.e1->ival >= 0 && hi.k == EK::Call && hi.s == "len" && hi.args.size() == 1 &&
                    hi.args[0]->k == EK::Var && hi.args[0]->slot >= 0 && !mutableIds.count(hi.args[0]->slot)) {
                    s.safeArr = hi.args[0]->slot;
                    markSafe(s.body, s.safeArr, s.slot);
                }
                break;
            }
            case SK::Return: {
                if (s.e1) {
                    Ty t = expr(*s.e1, false, curRet == TyUnit ? TyErr : curRet);
                    if (curRet == TyUnit) err(s.pos, "this function returns nothing, so `return` cannot carry a value");
                    else if (t != TyErr && !assignable(curRet, t))
                        err(s.e1->pos, std::string("expected `") + tyName(curRet) + "`, found `" + tyName(t) + "`");
                } else if (curRet != TyUnit) {
                    err(s.pos, std::string("this function must return `") + tyName(curRet) + "`");
                }
                break;
            }
            case SK::Break:
            case SK::Continue:
                if (loopDepth == 0) err(s.pos, s.k == SK::Break ? "`break` outside of a loop" : "`continue` outside of a loop");
                break;
            case SK::ExprS:
                if (s.e1->k != EK::Call && s.e1->k != EK::CallInd) err(s.e1->pos, "this expression has no effect; only function calls can be statements");
                expr(*s.e1, true);
                break;
            case SK::Block: block(s.body); break;
            case SK::Match: matchStmt(s); break;
        }
    }

    bool alwaysReturns(const std::vector<StmtP>& b) const {
        for (auto& s : b) {
            if (s->k == SK::Return) return true;
            if (s->k == SK::ExprS && s->e1->k == EK::Call && (s->e1->s == "panic" || s->e1->s == "exit") &&
                !sigs.count(s->e1->s))
                return true;  // never returns
            if (s->k == SK::Block && alwaysReturns(s->body)) return true;
            if (s->k == SK::If && !s->els.empty() && alwaysReturns(s->body) && alwaysReturns(s->els)) return true;
            if (s->k == SK::Match && s->mut && !s->arms.empty()) {
                bool all = true;
                for (auto& a : s->arms) all = all && alwaysReturns(a.body);
                if (all) return true;
            }
        }
        return false;
    }

public:
    std::vector<CompileError> run(Program& p) {
        for (size_t i = 0; i < structs().size(); i++) {
            const StructInfo& si = structs()[i];
            if (si.templ >= 0) continue;  // instances repeat their template
            if (si.local == "int" || si.local == "float" || si.local == "bool" || si.local == "str" || si.local == "byte")
                err(si.pos, "`" + si.local + "` is a built-in type name");
            if (!si.isEnum && si.fnames.empty()) err(si.pos, "struct `" + si.local + "` needs at least one field");
            if (si.isEnum && si.vnames.empty()) err(si.pos, "enum `" + si.local + "` needs at least one variant");
            for (size_t f = 0; f < si.fnames.size(); f++)
                for (size_t g = 0; g < f; g++)
                    if (si.fnames[g] == si.fnames[f]) err(si.fpos[f], "duplicate field `" + si.fnames[f] + "` in struct `" + si.local + "`");
            for (size_t f = 0; f < si.vnames.size(); f++)
                for (size_t g = 0; g < f; g++)
                    if (si.vnames[g] == si.vnames[f]) err(si.vpos[f], "duplicate variant `" + si.vnames[f] + "` in enum `" + si.local + "`");
            if (si.isEnum && si.maxPayload() > 6) err(si.pos, "an enum variant carries at most 6 values");
        }
        for (auto& c : p.consts) {
            if (consts.count(c.name)) err(c.pos, "constant `" + c.name + "` is defined twice");
            else consts[c.name] = &c;
        }
        for (auto& f : p.funcs) {
            // the special built-ins (print, len, new, ...) are reserved everywhere; a plain runtime built-in such as
            // `pow` may be redefined, and the file's own definition then wins inside that file
            if (isSpecialBuiltin(f.local)) err(f.pos, "`" + f.local + "` is a built-in and cannot be redefined");
            else if (sigs.count(f.name)) err(f.pos, "function `" + f.name + "` is defined twice");
            Sig sg;
            for (auto& pr : f.params) sg.params.push_back(pr.ty);
            sg.ret = f.ret;
            sg.pos = f.pos;
            sg.tparams = f.tparams;
            sigs[f.name] = sg;
            if (f.params.size() > 6) err(f.pos, "functions are limited to 6 parameters");
        }
        auto m = sigs.find("main");
        if (m == sigs.end()) err(Pos{1, 1}, "the program needs an entry point: `fn main() -> int { ... }`");
        else if (!m->second.params.empty() || m->second.ret != TyInt || !m->second.tparams.empty())
            err(m->second.pos, "`main` must be declared as `fn main() -> int`");

        for (auto& f : p.funcs) {
            curUnit = f.unit;
            curFn = nullptr;
            capScope.clear();
            outer.clear();
            scopes.clear();
            mutableIds.clear();
            nonnullIds.clear();
            scopes.emplace_back();
            nextId = 0;
            loopDepth = 0;
            curRet = f.ret;
            for (auto& pr : f.params) declare(pr.pos, pr.name, pr.ty, false);
            block(f.body);
            f.nvars = nextId;
            if (f.ret != TyUnit && !alwaysReturns(f.body))
                err(f.pos, "function `" + f.local + "` must return `" + tyName(f.ret) + "` on every path");
        }
        return errs;
    }
    const std::vector<CompileError>& warnings() const { return warns; }
};

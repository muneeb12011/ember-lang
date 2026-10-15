// ast.hpp - syntax tree and types. The type checker fills in `ty` and `slot` so lowering needs no lookups.
#pragma once
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "diag.hpp"
#include "lexer.hpp"
#include "modules.hpp"

// ---- types -----------------------------------------------------------------------------------
// A type is an interned integer id, so type equality is plain `==`. Arrays, structs, enums and type parameters live
// in a table. `null` has its own type (TyNull) that is assignable to any struct, enum or array type.
//
// Generics: a generic struct or enum declaration is a *template* (it has type parameters, and its own id is never the
// type of a value). Writing `Vec<int>` creates an *instance*: an ordinary struct whose field types have the parameters
// substituted. Instances are interned by (template, arguments). Inside a generic function, `Vec<T>` is an instance
// whose argument is the parameter type T; monomorphization substitutes it away before lowering.
using Ty = int;
enum : Ty { TyErr = 0, TyInt, TyBool, TyStr, TyUnit, TyFloat, TyByte, TyNull, TyIntArr, TyFloatArr };

struct TypeInfo {
    enum Kind { Scalar, Array, Struct, Enum, Param, Fn } kind = Scalar;
    Ty elem = TyErr;   // Array: element type; Fn: return type
    std::vector<Ty> params;  // Fn: parameter types
    std::string name;  // display name (mangled for structs and enums, e.g. "vec.Vec<int>")
    int ref = -1;      // Struct / Enum: index into the struct table
};

// A struct or an enum (they share one table: both are heap blocks addressed by field / payload index).
struct StructDef {
    std::string name;   // mangled (unique program-wide), e.g. "vec.IntVec" or "option.Option<int>"
    std::string local;  // as declared in its file
    int unit = 0;
    bool pub = false;
    bool isEnum = false;
    Ty ty = TyErr;
    Pos pos;
    std::vector<std::string> fnames;  // struct fields
    std::vector<Ty> ftys;
    std::vector<Pos> fpos;
    std::vector<std::string> vnames;  // enum variants and their payload types
    std::vector<std::vector<Ty>> vtys;
    std::vector<Pos> vpos;
    std::vector<Ty> tparams;  // non-empty: this is a generic template
    int templ = -1;           // instance: the template it came from
    std::vector<Ty> targs;
    bool complete = false;    // fields / variants known
    bool isTemplate() const { return !tparams.empty(); }
    int maxPayload() const {
        size_t m = 0;
        for (auto& v : vtys) m = std::max(m, v.size());
        return (int)m;
    }
};
using StructInfo = StructDef;

struct TypeTable {
    std::vector<TypeInfo> t;
    std::vector<StructDef> structs;
    std::map<std::pair<int, std::vector<Ty>>, Ty> instances;
    int instDepth = 0;

    TypeTable() {
        for (const char* n : {"<error>", "int", "bool", "str", "()", "float", "byte", "null"}) {
            TypeInfo ti;
            ti.name = n;
            t.push_back(ti);
        }
        arrayOf(TyInt);    // == TyIntArr
        arrayOf(TyFloat);  // == TyFloatArr
    }
    Ty arrayOf(Ty e) {
        for (size_t i = 0; i < t.size(); i++)
            if (t[i].kind == TypeInfo::Array && t[i].elem == e) return (Ty)i;
        TypeInfo ti;
        ti.kind = TypeInfo::Array;
        ti.elem = e;
        t.push_back(ti);
        return (Ty)t.size() - 1;
    }
    // function type fn(params) -> ret (ret == TyUnit: returns nothing)
    Ty fnType(const std::vector<Ty>& ps, Ty ret) {
        for (size_t i = 0; i < t.size(); i++)
            if (t[i].kind == TypeInfo::Fn && t[i].elem == ret && t[i].params == ps) return (Ty)i;
        TypeInfo ti;
        ti.kind = TypeInfo::Fn;
        ti.elem = ret;
        ti.params = ps;
        t.push_back(ti);
        return (Ty)t.size() - 1;
    }
    Ty newParam(const std::string& n) {
        TypeInfo ti;
        ti.kind = TypeInfo::Param;
        ti.name = n;
        t.push_back(ti);
        return (Ty)t.size() - 1;
    }
    // Always registers (duplicates are reported when the name is declared).
    int declareStruct(const std::string& n, Pos p, const std::string& local = "", int unit = 0, bool pub = false,
                      bool isEnum = false, std::vector<Ty> tparams = {}) {
        TypeInfo ti;
        ti.kind = isEnum ? TypeInfo::Enum : TypeInfo::Struct;
        ti.name = n;
        ti.ref = (int)structs.size();
        t.push_back(ti);
        StructDef d;
        d.name = n;
        d.local = local.empty() ? n : local;
        d.unit = unit;
        d.pub = pub;
        d.isEnum = isEnum;
        d.ty = (Ty)t.size() - 1;
        d.pos = p;
        d.tparams = std::move(tparams);
        structs.push_back(d);
        return (int)structs.size() - 1;
    }
    std::string name(Ty ty) const {
        if (ty < 0 || ty >= (Ty)t.size()) return "<error>";
        if (t[ty].kind == TypeInfo::Array) return "[" + name(t[ty].elem) + "]";
        if (t[ty].kind == TypeInfo::Fn) {
            std::string r = "fn(";
            for (size_t i = 0; i < t[ty].params.size(); i++) r += (i ? ", " : "") + name(t[ty].params[i]);
            r += ")";
            if (t[ty].elem != TyUnit) r += " -> " + name(t[ty].elem);
            return r;
        }
        return t[ty].name;
    }

    // ---- generics
    bool hasParams(Ty ty) const {
        const TypeInfo& ti = t[ty];
        if (ti.kind == TypeInfo::Param) return true;
        if (ti.kind == TypeInfo::Array) return hasParams(ti.elem);
        if (ti.kind == TypeInfo::Fn) {
            for (Ty p : ti.params)
                if (hasParams(p)) return true;
            return hasParams(ti.elem);
        }
        if ((ti.kind == TypeInfo::Struct || ti.kind == TypeInfo::Enum) && ti.ref >= 0)
            for (Ty a : structs[ti.ref].targs)
                if (hasParams(a)) return true;
        return false;
    }
    Ty subst(Ty ty, const std::map<Ty, Ty>& m) {
        if (m.empty() || ty < 0) return ty;
        const TypeInfo ti = t[ty];
        if (ti.kind == TypeInfo::Param) {
            auto it = m.find(ty);
            return it == m.end() ? ty : it->second;
        }
        if (ti.kind == TypeInfo::Array) return arrayOf(subst(ti.elem, m));
        if (ti.kind == TypeInfo::Fn) {
            std::vector<Ty> ps;
            for (Ty p : ti.params) ps.push_back(subst(p, m));
            return fnType(ps, subst(ti.elem, m));
        }
        if ((ti.kind == TypeInfo::Struct || ti.kind == TypeInfo::Enum) && ti.ref >= 0 && structs[ti.ref].templ >= 0) {
            std::vector<Ty> a = structs[ti.ref].targs;
            for (Ty& x : a) x = subst(x, m);
            return instantiate(structs[ti.ref].templ, a);
        }
        return ty;
    }
    // the type `template<args>` (the caller checked the argument count)
    Ty instantiate(int templ, std::vector<Ty> args) {  // by value: callers may pass a vector that lives in the table
        auto key = std::make_pair(templ, args);
        auto it = instances.find(key);
        if (it != instances.end()) return it->second;
        if (instDepth > 40 || instances.size() > 20000)
            throw CompileError{structs[templ].pos, "generic type `" + structs[templ].local + "` is instantiated without end "
                                                     "(a type that contains a bigger instance of itself)"};
        std::string nm = structs[templ].name + "<";
        for (size_t i = 0; i < args.size(); i++) nm += (i ? ", " : "") + name(args[i]);
        nm += ">";
        StructDef tp = structs[templ];
        int idx = declareStruct(nm, tp.pos, tp.local, tp.unit, tp.pub, tp.isEnum);
        structs[idx].templ = templ;
        structs[idx].targs = args;
        Ty ty = structs[idx].ty;
        instances[key] = ty;
        if (structs[templ].complete) fill(idx);
        return ty;
    }
    void fill(int idx) {
        int templ = structs[idx].templ;
        std::map<Ty, Ty> m;
        for (size_t i = 0; i < structs[templ].tparams.size(); i++) m[structs[templ].tparams[i]] = structs[idx].targs[i];
        instDepth++;
        std::vector<Ty> ftys;
        std::vector<Ty> tf = structs[templ].ftys;
        for (Ty f : tf) ftys.push_back(subst(f, m));
        std::vector<std::vector<Ty>> vtys;
        std::vector<std::vector<Ty>> tv = structs[templ].vtys;
        for (auto& v : tv) {
            vtys.emplace_back();
            for (Ty x : v) vtys.back().push_back(subst(x, m));
        }
        instDepth--;
        StructDef& d = structs[idx];  // (the table may have grown above)
        const StructDef& tp = structs[templ];
        d.fnames = tp.fnames;
        d.fpos = tp.fpos;
        d.ftys = ftys;
        d.vnames = tp.vnames;
        d.vpos = tp.vpos;
        d.vtys = vtys;
        d.complete = true;
    }
    // a template's body has been parsed: fill in the instances created before that
    void completeTemplate(int templ) {
        structs[templ].complete = true;
        for (size_t i = 0; i < structs.size(); i++)
            if (structs[i].templ == templ && !structs[i].complete) fill((int)i);
    }
};

inline TypeTable& types() {
    static TypeTable tt;
    return tt;
}
inline std::vector<StructDef>& structs() { return types().structs; }
inline std::string tyName(Ty ty) { return types().name(ty); }
inline bool validTy(Ty ty) { return ty >= 0 && ty < (Ty)types().t.size(); }
inline bool isArr(Ty ty) { return validTy(ty) && types().t[ty].kind == TypeInfo::Array; }
inline bool isStruct(Ty ty) { return validTy(ty) && types().t[ty].kind == TypeInfo::Struct; }
inline bool isEnum(Ty ty) { return validTy(ty) && types().t[ty].kind == TypeInfo::Enum; }
inline bool isParam(Ty ty) { return validTy(ty) && types().t[ty].kind == TypeInfo::Param; }
inline bool isFn(Ty ty) { return validTy(ty) && types().t[ty].kind == TypeInfo::Fn; }
inline const std::vector<Ty>& fnParams(Ty ty) { return types().t[ty].params; }
inline Ty fnRet(Ty ty) { return types().t[ty].elem; }
inline Ty fnType(const std::vector<Ty>& ps, Ty ret) { return types().fnType(ps, ret); }
inline Ty elemTy(Ty ty) { return types().t[ty].elem; }
inline Ty arrayOf(Ty e) { return types().arrayOf(e); }
inline int structId(Ty ty) {
    if (!validTy(ty)) return -1;
    const TypeInfo& ti = types().t[ty];
    return (ti.kind == TypeInfo::Struct || ti.kind == TypeInfo::Enum) ? ti.ref : -1;
}
inline int structIndex(const std::string& n) {
    for (size_t i = 0; i < structs().size(); i++)
        if (structs()[i].name == n) return (int)i;
    return -1;
}
inline Ty structTy(int idx) { return structs()[idx].ty; }
// [byte] elements are read and written as ints (stored as one byte, zero-extended on load)
inline Ty valTy(Ty elem) { return elem == TyByte ? TyInt : elem; }
// byte width of one element of an array (or of a str, which is a byte array)
inline int elemWidth(Ty arr) {
    if (arr == TyStr) return 1;
    if (isArr(arr)) { Ty e = elemTy(arr); return (e == TyByte || e == TyBool) ? 1 : 8; }
    return 8;
}
// does a value of this type hold a heap pointer? (the collector must scan objects that contain such values)
inline bool hasPointers(Ty t) { return t == TyStr || isArr(t) || isStruct(t) || isEnum(t) || t == TyNull || isParam(t) || isFn(t); }
inline bool isRef(Ty t) { return isStruct(t) || isArr(t) || isEnum(t) || isFn(t); }

// FnRef: a top-level function used as a value (s = mangled name, targs for a generic one)
// Lambda: an anonymous function `fn(x: int) -> int { ... }` (its declaration is in `fn`)
// CallInd: a call through a function value: args[0] is the callee, args[1..] the arguments
enum class EK { Int, Bool, Str, Var, Unary, Binary, Call, Index, Float, Field, StructLit, Null, EnumCtor, ArrayLit,
                FnRef, Lambda, CallInd };
struct FnDecl;

struct Expr;
using ExprP = std::unique_ptr<Expr>;

struct Expr {
    EK k;
    Pos pos;
    Ty ty = TyErr;
    long long ival = 0;       // Int / Bool literal; Index: 1 = proven in range, no runtime check
    double fval = 0;          // Float literal
    std::string s;            // Str literal, Var name, Call name (mangled once resolved), StructLit / EnumCtor type name
    std::string q;            // module alias written before the name (`math` in `math.abs(x)`), "" if none
    T op = T::End;            // Unary / Binary operator
    std::vector<ExprP> args;  // Unary:[x]  Binary:[l,r]  Call:[args...]  Index:[array,index]  EnumCtor:[payload...]
    int slot = -1;            // Var: variable id assigned by the checker; Field: field index; EnumCtor: variant index
    std::vector<std::string> names;  // StructLit: field names as written; EnumCtor: [variant]
    std::vector<int> idx;            // StructLit: declaration index of each written field
    std::vector<Ty> targs;           // Call / StructLit / EnumCtor / FnRef: type arguments (written or inferred)
    bool hasTargs = false;           // type arguments were written explicitly
    std::shared_ptr<FnDecl> fn;      // Lambda
};

struct Stmt;
using StmtP = std::unique_ptr<Stmt>;

struct MatchArm {
    std::string variant;  // "_" for the wildcard arm
    Pos pos;
    std::vector<std::string> binds;  // "_" ignores that payload value
    std::vector<Pos> bpos;
    std::vector<int> slots;  // variable ids of the bindings (-1 for "_")
    int vidx = -1;
    std::vector<StmtP> body;
};

enum class SK { Let, Assign, If, While, For, Return, Break, Continue, ExprS, Block, Match };

struct Stmt {
    SK k;
    Pos pos;
    std::string name;         // Let / For variable
    bool mut = false;         // Let
    bool hasTy = false;       // Let had an annotation
    Ty declTy = TyErr;
    ExprP e1, e2;             // Let:init  Assign:target,value  If/While:cond  For:lo,hi  Return:value  ExprS:call  Match:value
    std::vector<StmtP> body;  // If-then / While / For / Block
    std::vector<StmtP> els;   // If-else
    int slot = -1;            // Let / For variable id
    int safeArr = -1;         // For: id of an immutable array whose length is the loop bound (bounds checks provably unneeded)
    std::vector<MatchArm> arms;
};

struct Param {
    std::string name;
    Ty ty;
    Pos pos;
};

struct FnDecl {
    std::string name;   // mangled
    std::string local;  // as written
    int unit = 0;
    bool pub = false;
    Pos pos;
    std::vector<Param> params;
    Ty ret = TyUnit;
    std::vector<StmtP> body;
    int nvars = 0;              // total variable ids used (params first)
    std::vector<Ty> tparams;    // generic function: its type parameters
    // lambdas: the closure object arrives as a hidden last parameter (variable id `cloId` == params.size()); each
    // captured outer variable is copied into the closure when it is created and loaded into variable `inner` on entry
    struct Capture { int outer, inner; Ty ty; std::string name; };
    std::vector<Capture> caps;
    int cloId = -1;
    std::string lname;          // lambdas: the IR function name given at lowering
};

struct ConstDecl {
    std::string name;   // mangled
    std::string local;
    int unit = 0;
    bool pub = false;
    Pos pos;
    Ty ty = TyErr;
    long long ival = 0;
    double fval = 0;
    std::string sval;
};

struct ImportDecl {
    std::string path;
    std::string alias;
    Pos pos;
};

struct Program {
    std::vector<FnDecl> funcs;
    std::vector<ConstDecl> consts;
    std::vector<ImportDecl> imports;
};

// ---- deep copies (used by compound-assignment desugaring and by monomorphization)
inline StmtP cloneStmt(const Stmt& s);
inline std::shared_ptr<FnDecl> cloneFn(const FnDecl& f);
inline ExprP cloneExpr(const Expr& e) {
    auto c = std::make_unique<Expr>();
    c->k = e.k; c->pos = e.pos; c->ty = e.ty; c->ival = e.ival; c->fval = e.fval; c->s = e.s; c->q = e.q; c->op = e.op;
    c->slot = e.slot; c->names = e.names; c->idx = e.idx; c->targs = e.targs; c->hasTargs = e.hasTargs;
    for (auto& a : e.args) c->args.push_back(cloneExpr(*a));
    if (e.fn) c->fn = cloneFn(*e.fn);
    return c;
}
inline std::vector<StmtP> cloneBlock(const std::vector<StmtP>& b) {
    std::vector<StmtP> out;
    for (auto& s : b) out.push_back(cloneStmt(*s));
    return out;
}
inline StmtP cloneStmt(const Stmt& s) {
    auto c = std::make_unique<Stmt>();
    c->k = s.k; c->pos = s.pos; c->name = s.name; c->mut = s.mut; c->hasTy = s.hasTy; c->declTy = s.declTy;
    if (s.e1) c->e1 = cloneExpr(*s.e1);
    if (s.e2) c->e2 = cloneExpr(*s.e2);
    c->body = cloneBlock(s.body);
    c->els = cloneBlock(s.els);
    c->slot = s.slot;
    c->safeArr = s.safeArr;
    for (const MatchArm& a : s.arms) {
        MatchArm m;
        m.variant = a.variant; m.pos = a.pos; m.binds = a.binds; m.bpos = a.bpos; m.slots = a.slots; m.vidx = a.vidx;
        m.body = cloneBlock(a.body);
        c->arms.push_back(std::move(m));
    }
    return c;
}
inline std::shared_ptr<FnDecl> cloneFn(const FnDecl& f) {
    auto c = std::make_shared<FnDecl>();
    c->name = f.name; c->local = f.local; c->unit = f.unit; c->pub = f.pub; c->pos = f.pos; c->params = f.params;
    c->ret = f.ret; c->body = cloneBlock(f.body); c->nvars = f.nvars; c->tparams = f.tparams; c->caps = f.caps;
    c->cloId = f.cloId; c->lname = f.lname;
    return c;
}

// ir.hpp - three-address intermediate representation and AST -> IR lowering.
//
// Every variable and temporary is a virtual register (an int64). Operands are a register or an immediate.
// Arrays are heap blocks [length, e0, e1, ...] and are passed around as a pointer in a register.
#pragma once
#include <cstring>
#include <sstream>
#include <unordered_map>

#include "ast.hpp"
#include "builtins.hpp"

struct Opd {
    enum K : int { None, Reg, Imm } k = None;
    long long v = 0;
    static Opd reg(long long r) { Opd o; o.k = Reg; o.v = r; return o; }
    static Opd imm(long long x) { Opd o; o.k = Imm; o.v = x; return o; }
    bool isReg() const { return k == Reg; }
    bool isImm() const { return k == Imm; }
};

enum class Op {
    Mov, Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge, Neg, Not,
    Label, Jmp, Br, Call, Ret,
    Load, Store, Len, NewArr, Str,
    PrintInt, PrintBool, PrintStr, ReadInt,
    FAdd, FSub, FMul, FDiv, FEq, FNe, FLt, FLe, FGt, FGe, FNeg, IToF, FToI, FSqrt, PrintFloat,
    Rt,                          // call into the C runtime: name = symbol, args = operands, dst = result (or -1)
    And, Or, Xor, Shl, Shr, BNot,  // bitwise; Shl/Shr use the count mod 64, Shr is arithmetic
    CallInd,  // call through a closure: a = closure, args = arguments (the closure goes last, as a hidden argument)
    FnAddr    // dst = the code address of function `name`
};

struct Inst {
    Op op = Op::Mov;
    int dst = -1;
    Opd a, b, c;
    int t1 = -1, t2 = -1;   // Label: id.  Jmp: target.  Br: true/false targets.
    std::string name;       // Call target
    std::vector<Opd> args;  // Call arguments
    int line = 0;           // source line, for runtime error messages
    bool nocheck = false;   // Load/Store: bounds check proven unnecessary
    bool nonnull = false;   // Load/Store/Len: base pointer proven non-null
    int w = 8;              // Load/Store/NewArr: element width in bytes (1 for byte/bool arrays and strings, else 8)
    bool noscan = false;    // NewArr: the object holds no pointers, so the collector never scans it
    bool ptr = false;       // Store: the value may be a heap pointer (the generational collector's write barrier)
    bool fresh = false;     // Store: into an object allocated just before, with no call in between (no barrier)
};

struct IRFunc {
    std::string name;
    int nparams = 0, nregs = 0, nlabels = 0;
    int line = 0;  // declaration line
    bool retUnit = true;
    std::vector<Inst> code;
};

struct Module {
    std::vector<IRFunc> funcs;
    std::vector<std::string> strings;
};

inline bool isCmp(Op o) { return o >= Op::Eq && o <= Op::Ge; }
inline bool isTerm(Op o) { return o == Op::Jmp || o == Op::Br || o == Op::Ret; }

// ---------------------------------------------------------------------------------------- printing
inline const char* opName(Op o) {
    static const char* n[] = {"mov", "add", "sub", "mul", "div", "mod", "eq", "ne", "lt", "le", "gt", "ge", "neg", "not",
                              "label", "jmp", "br", "call", "ret", "load", "store", "len", "newarr", "str",
                              "print_int", "print_bool", "print_str", "read_int",
                              "fadd", "fsub", "fmul", "fdiv", "feq", "fne", "flt", "fle", "fgt", "fge", "fneg", "itof", "ftoi", "fsqrt", "print_float",
                              "rt", "and", "or", "xor", "shl", "shr", "bnot", "callind", "fnaddr"};
    return n[(int)o];
}

inline std::string fmtOpd(const Opd& o) {
    if (o.isImm()) return std::to_string(o.v);
    if (o.isReg()) return "r" + std::to_string(o.v);
    return "_";
}

inline std::string fmtInst(const Inst& in) {
    std::ostringstream s;
    switch (in.op) {
        case Op::Label: s << "L" << in.t1 << ":"; return s.str();
        case Op::Jmp: s << "  jmp L" << in.t1; return s.str();
        case Op::Br: s << "  br " << fmtOpd(in.a) << ", L" << in.t1 << ", L" << in.t2; return s.str();
        case Op::Ret: s << "  ret" << (in.a.k == Opd::None ? "" : " " + fmtOpd(in.a)); return s.str();
        case Op::Store: s << "  store " << fmtOpd(in.a) << "[" << fmtOpd(in.b) << "] = " << fmtOpd(in.c) << (in.nocheck ? "   ; unchecked" : ""); return s.str();
        case Op::Call:
            s << "  ";
            if (in.dst >= 0) s << "r" << in.dst << " = ";
            s << "call " << in.name << "(";
            for (size_t i = 0; i < in.args.size(); i++) s << (i ? ", " : "") << fmtOpd(in.args[i]);
            s << ")";
            return s.str();
        case Op::Rt:
            s << "  ";
            if (in.dst >= 0) s << "r" << in.dst << " = ";
            s << "rt " << in.name << "(";
            for (size_t i = 0; i < in.args.size(); i++) s << (i ? ", " : "") << fmtOpd(in.args[i]);
            s << ")";
            return s.str();
        case Op::PrintInt: case Op::PrintBool: case Op::PrintStr: case Op::PrintFloat: s << "  " << opName(in.op) << " " << fmtOpd(in.a); return s.str();
        case Op::CallInd:
            s << "  ";
            if (in.dst >= 0) s << "r" << in.dst << " = ";
            s << "callind " << fmtOpd(in.a) << "(";
            for (size_t i = 0; i < in.args.size(); i++) s << (i ? ", " : "") << fmtOpd(in.args[i]);
            s << ")";
            return s.str();
        case Op::FnAddr: s << "  r" << in.dst << " = fnaddr " << in.name; return s.str();
        default: break;
    }
    s << "  r" << in.dst << " = " << opName(in.op);
    if (in.a.k != Opd::None) s << " " << fmtOpd(in.a);
    if (in.b.k != Opd::None) s << ", " << fmtOpd(in.b);
    if (in.op == Op::Load && in.nocheck) s << "   ; unchecked";
    return s.str();
}

inline std::string fmtFunc(const IRFunc& f) {
    std::ostringstream s;
    s << "func " << f.name << "(" << f.nparams << " params, " << f.nregs << " regs)\n";
    for (auto& in : f.code) s << fmtInst(in) << "\n";
    return s.str();
}

inline int countInsts(const Module& m) {
    int n = 0;
    for (auto& f : m.funcs)
        for (auto& in : f.code)
            if (in.op != Op::Label) n++;
    return n;
}

// ---------------------------------------------------------------------------------------- lowering
class Lowerer {
    Module mod;
    IRFunc* f = nullptr;
    struct Loop { int cont, brk; };
    std::vector<Loop> loops;
    std::unordered_map<std::string, int> strIdx;
    std::unordered_map<std::string, const FnDecl*> decls;
    std::vector<FnDecl*> lambdas;           // anonymous functions waiting to be lowered as functions of their own
    std::vector<std::string> wrappers;      // top-level functions used as values (each gets a closure-convention wrapper)
    std::unordered_map<std::string, int> wrapperIdx;

    int curLine = 0;

    int newReg() { return f->nregs++; }
    int newLabel() { return f->nlabels++; }
    Inst& emit(Op op, int line = 0) {
        f->code.emplace_back();
        Inst& in = f->code.back();
        in.op = op;
        in.line = line ? line : curLine;  // every instruction carries a source line (debug info, error messages)
        return in;
    }
    void label(int id) { emit(Op::Label).t1 = id; }
    void jmp(int id) { emit(Op::Jmp).t1 = id; }
    void br(Opd c, int t, int e) {
        Inst& in = emit(Op::Br);
        in.a = c;
        in.t1 = t;
        in.t2 = e;
    }
    int strId(const std::string& s) {
        auto it = strIdx.find(s);
        if (it != strIdx.end()) return it->second;
        int id = (int)mod.strings.size();
        mod.strings.push_back(s);
        strIdx[s] = id;
        return id;
    }
    Opd rt(const char* name, std::vector<Opd> args, bool hasResult = true, int line = 0) {
        int r = hasResult ? newReg() : -1;
        Inst& in = emit(Op::Rt, line);
        in.dst = r;
        in.name = name;
        in.args = std::move(args);
        return r >= 0 ? Opd::reg(r) : Opd::imm(0);
    }
    Opd bin(Op op, Opd a, Opd b, int line = 0) {
        int r = newReg();
        Inst& in = emit(op, line);
        in.dst = r;
        in.a = a;
        in.b = b;
        return Opd::reg(r);
    }

    Opd expr(Expr& e) {
        switch (e.k) {
            case EK::Int: return Opd::imm(e.ival);
            case EK::Bool: return Opd::imm(e.ival);
            case EK::Float: { long long b; std::memcpy(&b, &e.fval, 8); return Opd::imm(b); }
            case EK::Str: {
                int r = newReg();
                Inst& in = emit(Op::Str);
                in.dst = r;
                in.a = Opd::imm(strId(e.s));
                return Opd::reg(r);
            }
            case EK::Var: return Opd::reg(e.slot);
            case EK::Unary: {
                Opd a = expr(*e.args[0]);
                int r = newReg();
                Inst& in = emit(e.op == T::Minus ? (e.args[0]->ty == TyFloat ? Op::FNeg : Op::Neg)
                                : e.op == T::Tilde ? Op::BNot : Op::Not);
                in.dst = r;
                in.a = a;
                return Opd::reg(r);
            }
            case EK::Binary: {
                if (e.op == T::AndAnd || e.op == T::OrOr) {
                    int r = newReg(), rhs = newLabel(), end = newLabel();
                    Opd a = expr(*e.args[0]);
                    Inst& m = emit(Op::Mov);
                    m.dst = r;
                    m.a = a;
                    if (e.op == T::AndAnd) br(Opd::reg(r), rhs, end);
                    else br(Opd::reg(r), end, rhs);
                    label(rhs);
                    Opd b = expr(*e.args[1]);
                    Inst& m2 = emit(Op::Mov);
                    m2.dst = r;
                    m2.a = b;
                    label(end);
                    return Opd::reg(r);
                }
                Opd a = expr(*e.args[0]);
                Opd b = expr(*e.args[1]);
                Op op = Op::Add;
                switch (e.op) {
                    case T::Plus: op = Op::Add; break;
                    case T::Minus: op = Op::Sub; break;
                    case T::Star: op = Op::Mul; break;
                    case T::Slash: op = Op::Div; break;
                    case T::Percent: op = Op::Mod; break;
                    case T::Amp: op = Op::And; break;
                    case T::Pipe: op = Op::Or; break;
                    case T::Caret: op = Op::Xor; break;
                    case T::Shl: op = Op::Shl; break;
                    case T::Shr: op = Op::Shr; break;
                    case T::Eq: op = Op::Eq; break;
                    case T::Ne: op = Op::Ne; break;
                    case T::Lt: op = Op::Lt; break;
                    case T::Le: op = Op::Le; break;
                    case T::Gt: op = Op::Gt; break;
                    default: op = Op::Ge; break;
                }
                if (e.args[0]->ty == TyStr) {
                    if (e.op == T::Plus) return rt("ember_str_concat", {a, b}, true, e.pos.line);
                    Opd c = rt("ember_str_cmp", {a, b}, true, e.pos.line);  // -1 / 0 / 1
                    return bin(op, c, Opd::imm(0), e.pos.line);
                }
                if (e.args[0]->ty == TyFloat) {
                    switch (op) {
                        case Op::Add: op = Op::FAdd; break;
                        case Op::Sub: op = Op::FSub; break;
                        case Op::Mul: op = Op::FMul; break;
                        case Op::Div: op = Op::FDiv; break;
                        case Op::Eq: op = Op::FEq; break;
                        case Op::Ne: op = Op::FNe; break;
                        case Op::Lt: op = Op::FLt; break;
                        case Op::Le: op = Op::FLe; break;
                        case Op::Gt: op = Op::FGt; break;
                        default: op = Op::FGe; break;
                    }
                }
                return bin(op, a, b, e.pos.line);
            }
            case EK::Index: {
                Opd a = expr(*e.args[0]);
                Opd i = expr(*e.args[1]);
                int r = newReg();
                Inst& in = emit(Op::Load, e.pos.line);
                in.dst = r;
                in.a = a;
                in.b = i;
                in.nocheck = (e.ival & 1) != 0;
                in.nonnull = (e.ival & 2) != 0;
                in.w = elemWidth(e.args[0]->ty);
                return Opd::reg(r);
            }
            case EK::Null: return Opd::imm(0);
            case EK::Field: {  // a struct is a heap block laid out like an array: field i is element i
                Opd a = expr(*e.args[0]);
                int r = newReg();
                Inst& in = emit(Op::Load, e.pos.line);
                in.dst = r;
                in.a = a;
                in.b = Opd::imm(e.slot);
                in.nocheck = true;
                in.nonnull = (e.ival & 2) != 0;
                return Opd::reg(r);
            }
            case EK::StructLit: {
                std::vector<Opd> vals;
                for (auto& a : e.args) vals.push_back(expr(*a));
                int r = newReg();
                Inst& na = emit(Op::NewArr, e.pos.line);
                na.dst = r;
                na.a = Opd::imm((long long)structs()[structId(e.ty)].fnames.size());
                na.noscan = true;
                for (Ty ft : structs()[structId(e.ty)].ftys)
                    if (hasPointers(ft)) na.noscan = false;
                for (size_t i = 0; i < vals.size(); i++) {
                    Inst& st = emit(Op::Store, e.pos.line);
                    st.a = Opd::reg(r);
                    st.b = Opd::imm(e.idx[i]);
                    st.c = vals[i];
                    st.nocheck = true;
                    st.nonnull = true;
                    st.fresh = true;
                }
                return Opd::reg(r);
            }
            case EK::EnumCtor: {  // an enum value is a heap block [tag, payload...] sized for the largest variant
                std::vector<Opd> vals;
                for (auto& a : e.args) vals.push_back(expr(*a));
                const StructDef& d = structs()[structId(e.ty)];
                int r = newReg();
                Inst& na = emit(Op::NewArr, e.pos.line);
                na.dst = r;
                na.a = Opd::imm(1 + d.maxPayload());
                na.noscan = true;
                for (auto& v : d.vtys)
                    for (Ty t : v)
                        if (hasPointers(t)) na.noscan = false;
                Inst& tag = emit(Op::Store, e.pos.line);
                tag.a = Opd::reg(r);
                tag.b = Opd::imm(0);
                tag.c = Opd::imm(e.slot);
                tag.nocheck = tag.nonnull = tag.fresh = true;
                for (size_t i = 0; i < vals.size(); i++) {
                    Inst& st = emit(Op::Store, e.pos.line);
                    st.a = Opd::reg(r);
                    st.b = Opd::imm((long long)i + 1);
                    st.c = vals[i];
                    st.nocheck = st.nonnull = st.fresh = true;
                }
                return Opd::reg(r);
            }
            case EK::FnRef: {  // a closure with no captures whose code is a wrapper around the function
                if (!wrapperIdx.count(e.s)) {
                    wrapperIdx[e.s] = (int)wrappers.size();
                    wrappers.push_back(e.s);
                }
                return closure(e.s + "$fn", {}, e.pos.line, true);
            }
            case EK::Lambda: {
                FnDecl& L = *e.fn;
                L.lname = f->name + "$fn" + std::to_string(f->nlabels++);  // (a label id is a convenient unique number)
                lambdas.push_back(&L);
                std::vector<Opd> caps;
                bool noscan = true;
                for (auto& c : L.caps) {
                    caps.push_back(Opd::reg(c.outer));
                    if (hasPointers(c.ty)) noscan = false;
                }
                return closure(L.lname, caps, e.pos.line, noscan);
            }
            case EK::CallInd: {
                Opd callee = expr(*e.args[0]);
                std::vector<Opd> args;
                for (size_t i = 1; i < e.args.size(); i++) args.push_back(expr(*e.args[i]));
                int r = fnRet(e.args[0]->ty) == TyUnit ? -1 : newReg();
                Inst& in = emit(Op::CallInd, e.pos.line);
                in.dst = r;
                in.a = callee;
                in.args = std::move(args);
                in.nonnull = (e.ival & 2) != 0;
                return r >= 0 ? Opd::reg(r) : Opd::imm(0);
            }
            case EK::ArrayLit: {
                std::vector<Opd> vals;
                for (auto& a : e.args) vals.push_back(expr(*a));
                int r = newReg();
                Inst& na = emit(Op::NewArr, e.pos.line);
                na.dst = r;
                na.a = Opd::imm((long long)vals.size());
                na.w = elemWidth(e.ty);
                na.noscan = !hasPointers(elemTy(e.ty));
                for (size_t i = 0; i < vals.size(); i++) {
                    Inst& st = emit(Op::Store, e.pos.line);
                    st.a = Opd::reg(r);
                    st.b = Opd::imm((long long)i);
                    st.c = vals[i];
                    st.w = elemWidth(e.ty);
                    st.nocheck = st.nonnull = st.fresh = true;
                }
                return Opd::reg(r);
            }
            case EK::Call: return call(e);
        }
        return Opd::imm(0);
    }

    // closure object: [code, captured values...]
    Opd closure(const std::string& code, const std::vector<Opd>& caps, int line, bool noscan) {
        int fa = newReg();
        Inst& a = emit(Op::FnAddr, line);
        a.dst = fa;
        a.name = code;
        int r = newReg();
        Inst& na = emit(Op::NewArr, line);
        na.dst = r;
        na.a = Opd::imm(1 + (long long)caps.size());
        na.noscan = noscan;
        Inst& st = emit(Op::Store, line);
        st.a = Opd::reg(r);
        st.b = Opd::imm(0);
        st.c = Opd::reg(fa);
        st.nocheck = st.nonnull = st.fresh = true;
        for (size_t i = 0; i < caps.size(); i++) {
            Inst& sc = emit(Op::Store, line);
            sc.a = Opd::reg(r);
            sc.b = Opd::imm((long long)i + 1);
            sc.c = caps[i];
            sc.nocheck = sc.nonnull = sc.fresh = true;
        }
        return Opd::reg(r);
    }

    Opd call(Expr& e) {
        if (e.s == "print") {
            Opd a = expr(*e.args[0]);
            Ty t = e.args[0]->ty;
            emit(t == TyInt ? Op::PrintInt : t == TyFloat ? Op::PrintFloat : t == TyBool ? Op::PrintBool : Op::PrintStr).a = a;
            return Opd::imm(0);
        }
        if (e.s == "len") {
            Opd a = expr(*e.args[0]);
            int r = newReg();
            Inst& in = emit(Op::Len);
            in.dst = r;
            in.a = a;
            in.nonnull = (e.ival & 2) != 0;
            return Opd::reg(r);
        }
        if (e.s == "new_array" || e.s == "new_floats") {
            Opd a = expr(*e.args[0]);
            int r = newReg();
            Inst& in = emit(Op::NewArr, e.pos.line);
            in.dst = r;
            in.a = a;
            in.noscan = true;  // [int] / [float]
            return Opd::reg(r);
        }
        if (e.s == "float" || e.s == "int" || e.s == "sqrt") {
            Opd a = expr(*e.args[0]);
            int r = newReg();
            Inst& in = emit(e.s == "float" ? Op::IToF : e.s == "int" ? Op::FToI : Op::FSqrt);
            in.dst = r;
            in.a = a;
            return Opd::reg(r);
        }
        if (e.s == "read_int") {
            int r = newReg();
            emit(Op::ReadInt).dst = r;
            return Opd::reg(r);
        }
        if (e.s == "new") {
            Ty at = (Ty)e.slot, el = elemTy(at);
            Opd n = expr(*e.args[0]);
            if (el == TyStr) return rt("ember_new_str_array", {n, Opd::imm(e.pos.line)}, true, e.pos.line);  // elements start as ""
            int r = newReg();
            Inst& in = emit(Op::NewArr, e.pos.line);
            in.dst = r;
            in.a = n;
            in.w = elemWidth(at);
            in.noscan = !hasPointers(el);
            return Opd::reg(r);
        }
        if (e.s == "str") {
            Opd a = expr(*e.args[0]);
            switch (e.args[0]->ty) {
                case TyInt: return rt("ember_str_from_int", {a});
                case TyFloat: return rt("ember_str_from_float", {a});
                case TyBool: return rt("ember_str_from_bool", {a});
                default: return a;
            }
        }
        if (e.s == "assert") {
            Opd c = expr(*e.args[0]);
            int ok = newLabel(), bad = newLabel();
            br(c, ok, bad);
            label(bad);
            rt("ember_panic", {Opd::imm(5), Opd::imm(e.pos.line)}, false, e.pos.line);
            label(ok);
            return Opd::imm(0);
        }
        const Builtin* bi = decls.count(e.s) ? nullptr : findBuiltin(e.s);  // a user function may shadow a runtime built-in
        if (bi) {
            std::vector<Opd> args;
            for (auto& a : e.args) args.push_back(expr(*a));
            if (bi->fmath >= 0) {
                args.insert(args.begin(), Opd::imm(bi->fmath));
                if (args.size() == 2) args.push_back(Opd::imm(0));
            }
            if (bi->passLine) args.push_back(Opd::imm(e.pos.line));
            return rt(bi->rt, std::move(args), bi->ret != TyUnit, e.pos.line);
        }
        std::vector<Opd> args;
        for (auto& a : e.args) args.push_back(expr(*a));
        bool unit = decls.at(e.s)->ret == TyUnit;
        int r = unit ? -1 : newReg();
        Inst& in = emit(Op::Call, e.pos.line);
        in.dst = r;
        in.name = e.s;
        in.args = std::move(args);
        return r >= 0 ? Opd::reg(r) : Opd::imm(0);
    }

    // Branch on a condition without materialising booleans for && || !
    void cond(Expr& e, int lt, int lf) {
        if (e.k == EK::Binary && e.op == T::AndAnd) {
            int mid = newLabel();
            cond(*e.args[0], mid, lf);
            label(mid);
            cond(*e.args[1], lt, lf);
            return;
        }
        if (e.k == EK::Binary && e.op == T::OrOr) {
            int mid = newLabel();
            cond(*e.args[0], lt, mid);
            label(mid);
            cond(*e.args[1], lt, lf);
            return;
        }
        if (e.k == EK::Unary && e.op == T::Bang) {
            cond(*e.args[0], lf, lt);
            return;
        }
        if (e.k == EK::Bool) {
            jmp(e.ival ? lt : lf);
            return;
        }
        br(expr(e), lt, lf);
    }

    void block(std::vector<StmtP>& b) {
        for (auto& s : b) stmt(*s);
    }

    void stmt(Stmt& s) {
        curLine = s.pos.line;
        switch (s.k) {
            case SK::Let: {
                Opd v = expr(*s.e1);
                Inst& in = emit(Op::Mov);
                in.dst = s.slot;
                in.a = v;
                break;
            }
            case SK::Assign: {
                Expr& tg = *s.e1;
                if (tg.k == EK::Var) {
                    Opd v = expr(*s.e2);
                    Inst& in = emit(Op::Mov);
                    in.dst = tg.slot;
                    in.a = v;
                } else if (tg.k == EK::Field) {
                    Opd a = expr(*tg.args[0]);
                    Opd v = expr(*s.e2);
                    Inst& in = emit(Op::Store, tg.pos.line);
                    in.a = a;
                    in.b = Opd::imm(tg.slot);
                    in.c = v;
                    in.nocheck = true;
                    in.nonnull = (tg.ival & 2) != 0;
                    in.ptr = hasPointers(tg.ty);
                } else {
                    Opd a = expr(*tg.args[0]);
                    Opd i = expr(*tg.args[1]);
                    Opd v = expr(*s.e2);
                    Inst& in = emit(Op::Store, tg.pos.line);
                    in.a = a;
                    in.b = i;
                    in.c = v;
                    in.nocheck = (tg.ival & 1) != 0;
                    in.nonnull = (tg.ival & 2) != 0;
                    in.w = elemWidth(tg.args[0]->ty);
                    in.ptr = hasPointers(tg.ty);
                }
                break;
            }
            case SK::If: {
                int lt = newLabel(), le = newLabel(), end = s.els.empty() ? le : newLabel();
                cond(*s.e1, lt, le);
                label(lt);
                block(s.body);
                if (!s.els.empty()) {
                    jmp(end);
                    label(le);
                    block(s.els);
                }
                label(end);
                break;
            }
            case SK::While: {
                int lc = newLabel(), lb = newLabel(), lx = newLabel();
                label(lc);
                cond(*s.e1, lb, lx);
                label(lb);
                loops.push_back({lc, lx});
                block(s.body);
                loops.pop_back();
                jmp(lc);
                label(lx);
                break;
            }
            case SK::For: {
                Opd lo = expr(*s.e1);
                Opd hi = expr(*s.e2);
                int cnt = newReg(), end = newReg();
                Inst& m1 = emit(Op::Mov);
                m1.dst = cnt;
                m1.a = lo;
                Inst& m2 = emit(Op::Mov);
                m2.dst = end;
                m2.a = hi;
                int lc = newLabel(), lb = newLabel(), lstep = newLabel(), lx = newLabel();
                label(lc);
                Opd t = bin(Op::Lt, Opd::reg(cnt), Opd::reg(end));
                br(t, lb, lx);
                label(lb);
                Inst& m3 = emit(Op::Mov);
                m3.dst = s.slot;
                m3.a = Opd::reg(cnt);
                loops.push_back({lstep, lx});
                block(s.body);
                loops.pop_back();
                label(lstep);
                Inst& inc = emit(Op::Add);
                inc.dst = cnt;
                inc.a = Opd::reg(cnt);
                inc.b = Opd::imm(1);
                jmp(lc);
                label(lx);
                break;
            }
            case SK::Return: {
                if (s.e1) emit(Op::Ret).a = expr(*s.e1);
                else emit(Op::Ret);
                break;
            }
            case SK::Break: jmp(loops.back().brk); break;
            case SK::Continue: jmp(loops.back().cont); break;
            case SK::ExprS: expr(*s.e1); break;
            case SK::Block: block(s.body); break;
            case SK::Match: {
                Opd v = expr(*s.e1);
                if (!v.isReg()) {  // keep the value in one register for the tag test and the payload loads
                    int t = newReg();
                    Inst& m = emit(Op::Mov);
                    m.dst = t;
                    m.a = v;
                    v = Opd::reg(t);
                }
                int tag = newReg(), end = newLabel();
                Inst& ld = emit(Op::Load, s.pos.line);  // traps on null
                ld.dst = tag;
                ld.a = v;
                ld.b = Opd::imm(0);
                ld.nocheck = true;
                std::vector<int> armL;
                int wild = -1;
                for (size_t k = 0; k < s.arms.size(); k++) {
                    armL.push_back(newLabel());
                    if (s.arms[k].vidx < 0) {
                        wild = armL.back();
                        continue;
                    }
                    int next = newLabel();
                    Opd c = bin(Op::Eq, Opd::reg(tag), Opd::imm(s.arms[k].vidx), s.pos.line);
                    br(c, armL.back(), next);
                    label(next);
                }
                jmp(wild >= 0 ? wild : end);
                for (size_t k = 0; k < s.arms.size(); k++) {
                    MatchArm& a = s.arms[k];
                    label(armL[k]);
                    for (size_t j = 0; j < a.slots.size(); j++) {
                        if (a.slots[j] < 0) continue;
                        Inst& pl = emit(Op::Load, a.pos.line);
                        pl.dst = a.slots[j];
                        pl.a = v;
                        pl.b = Opd::imm((long long)j + 1);
                        pl.nocheck = pl.nonnull = true;
                    }
                    block(a.body);
                    jmp(end);
                }
                label(end);
                break;
            }
        }
    }

public:
    Module lower(Program& p) {
        for (auto& fn : p.funcs) decls[fn.name] = &fn;
        for (auto& fn : p.funcs) {
            mod.funcs.emplace_back();
            f = &mod.funcs.back();
            f->name = fn.name;
            f->line = curLine = fn.pos.line;
            f->nparams = (int)fn.params.size();
            f->nregs = fn.nvars;
            f->retUnit = fn.ret == TyUnit;
            loops.clear();
            block(fn.body);
            if (f->retUnit) emit(Op::Ret);
            else emit(Op::Ret).a = Opd::imm(0);  // unreachable: the checker proved every path returns
        }
        // anonymous functions (lowering one may queue more): parameters, then the closure, then the captures
        for (size_t k = 0; k < lambdas.size(); k++) {
            FnDecl& L = *lambdas[k];
            mod.funcs.emplace_back();
            f = &mod.funcs.back();
            f->name = L.lname;
            f->line = curLine = L.pos.line;
            f->nparams = (int)L.params.size() + 1;
            f->nregs = L.nvars;
            f->retUnit = L.ret == TyUnit;
            loops.clear();
            for (size_t c = 0; c < L.caps.size(); c++) {
                Inst& ld = emit(Op::Load, L.pos.line);
                ld.dst = L.caps[c].inner;
                ld.a = Opd::reg(L.cloId);
                ld.b = Opd::imm((long long)c + 1);
                ld.nocheck = ld.nonnull = true;
            }
            block(L.body);
            if (f->retUnit) emit(Op::Ret);
            else emit(Op::Ret).a = Opd::imm(0);
        }
        // wrappers that give top-level functions the closure calling convention (the closure argument is ignored)
        for (const std::string& w : wrappers) {
            const FnDecl& d = *decls.at(w);
            mod.funcs.emplace_back();
            f = &mod.funcs.back();
            int n = (int)d.params.size();
            f->name = w + "$fn";
            f->line = d.pos.line;
            f->nparams = n + 1;
            f->nregs = n + 2;
            f->retUnit = d.ret == TyUnit;
            Inst& c = emit(Op::Call, d.pos.line);
            c.name = w;
            for (int i = 0; i < n; i++) c.args.push_back(Opd::reg(i));
            c.dst = f->retUnit ? -1 : n + 1;
            if (f->retUnit) emit(Op::Ret);
            else emit(Op::Ret).a = Opd::reg(n + 1);
        }
        return std::move(mod);
    }
};

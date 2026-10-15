// parser.hpp - recursive-descent parser with precedence climbing. Throws CompileError on the first syntax error.
//
// Types are resolved while parsing (imports are loaded first, and a pre-scan registers this file's own structs and
// enums), so `Vec<int>` becomes an interned instance type right here. Inside a generic declaration its type
// parameters are in scope as opaque types.
#pragma once
#include <set>

#include "ast.hpp"

class Parser {
    std::vector<Token> t;
    size_t i = 0;
    int unit = 0;                         // the file being parsed (index into units())
    std::map<std::string, Ty> tscope;     // type parameters in scope
    std::set<std::string> ownFns, genericFns;  // this file's functions, and which of them are generic (pre-scanned)

    const Token& cur() const { return t[i]; }
    const Token& peek(size_t k) const { return t[std::min(i + k, t.size() - 1)]; }
    bool is(T k) const { return cur().k == k; }
    Token take() {
        Token x = t[i];
        if (i + 1 < t.size()) i++;
        return x;
    }
    bool accept(T k) {
        if (!is(k)) return false;
        take();
        return true;
    }
    static std::string describe(const Token& tk) {
        if (tk.k == T::End) return "end of file";
        if (tk.k == T::Str) return "a string";
        return "`" + tk.text + "`";
    }
    Token expect(T k, const std::string& what) {
        if (!is(k)) throw CompileError{cur().pos, "expected " + what + ", found " + describe(cur())};
        return take();
    }

    // ---- types ---------------------------------------------------------------------------------------------
    // `>` closing a type-argument list; `>>` (as in `Vec<Vec<int>>`) is split in two
    void closeAngle() {
        Token& c = t[i];
        if (c.k == T::Gt) { take(); return; }
        if (c.k == T::Shr) { c.k = T::Gt; c.text = ">"; c.pos.col++; return; }
        if (c.k == T::Ge) { c.k = T::Assign; c.text = "="; c.pos.col++; return; }
        if (c.k == T::ShrEq) { c.k = T::Ge; c.text = ">="; c.pos.col++; return; }
        throw CompileError{c.pos, "expected `>` to close the type arguments, found " + describe(c)};
    }
    std::vector<Ty> parseTypeArgs() {
        expect(T::Lt, "`<`");
        std::vector<Ty> a;
        do { a.push_back(parseType()); } while (accept(T::Comma));
        closeAngle();
        return a;
    }
    Ty instance(int idx, const std::vector<Ty>& args, Pos p) {
        const StructDef& d = structs()[idx];
        if (args.size() != d.tparams.size())
            throw CompileError{p, "`" + d.local + "` takes " + std::to_string(d.tparams.size()) + " type argument(s), got " +
                                      std::to_string(args.size())};
        return types().instantiate(idx, args);
    }

    Ty parseType() {
        if (accept(T::LBrack)) {
            Token first = cur();
            Ty e;
            if (first.k == T::Ident && first.text == "byte") { take(); e = TyByte; }
            else e = parseType();
            expect(T::RBrack, "`]`");
            return arrayOf(e);
        }
        if (accept(T::Fn)) {  // fn(int, str) -> bool
            expect(T::LParen, "`(` after `fn` in a function type");
            std::vector<Ty> ps;
            if (!is(T::RParen)) {
                do { ps.push_back(parseType()); } while (accept(T::Comma));
            }
            expect(T::RParen, "`)`");
            Ty ret = accept(T::Arrow) ? parseType() : TyUnit;
            return fnType(ps, ret);
        }
        Token n = expect(T::Ident, "a type");
        auto tp = tscope.find(n.text);
        if (tp != tscope.end()) return tp->second;
        if (isAlias(unit, n.text) && is(T::Dot)) {  // module.Type
            take();
            Token m = expect(T::Ident, "a type name after `" + n.text + ".`");
            return namedType(n.text, m);
        }
        if (n.text == "int") return TyInt;
        if (n.text == "bool") return TyBool;
        if (n.text == "float") return TyFloat;
        if (n.text == "str") return TyStr;
        if (n.text == "byte") throw CompileError{n.pos, "`byte` only exists as an array element type: write `[byte]`"};
        return namedType("", n);
    }

    Ty namedType(const std::string& q, const Token& n) {
        Resolved r = resolveSym(unit, q, n.text, SymK::Struct, n.pos);
        if (r.failed) throw r.err;
        if (!r.sym) throw CompileError{n.pos, "unknown type `" + n.text + "` (types are int, float, bool, str, [T] for any T, and declared structs and enums)"};
        int idx = structIndex(r.sym->mangled);
        const StructDef& d = structs()[idx];
        if (d.isTemplate()) {
            if (!is(T::Lt))
                throw CompileError{n.pos, "`" + d.local + "` is generic: write `" + d.local + "<...>` with " +
                                              std::to_string(d.tparams.size()) + " type argument(s)"};
            Pos p = cur().pos;
            return instance(idx, parseTypeArgs(), p);
        }
        if (is(T::Lt)) throw CompileError{cur().pos, "`" + d.local + "` is not generic, so it takes no type arguments"};
        return d.ty;
    }

    // a type name in expression position (struct literal, enum constructor); nullptr when the name is not a type
    const Sym* exprTypeSym(const std::string& q, const std::string& n, Pos p) {
        if (tscope.count(n) && q.empty()) return nullptr;
        Resolved r = resolveSym(unit, q, n, SymK::Struct, p);
        if (r.failed && r.hidden) throw r.err;  // a private type: say so rather than "unknown variable"
        return r.failed ? nullptr : r.sym;
    }
    bool isGenericFn(const std::string& q, const std::string& n, Pos p) {
        if (q.empty() && ownFns.count(n)) return genericFns.count(n) > 0;
        Resolved r = resolveSym(unit, q, n, SymK::Fn, p);
        return r.sym && r.sym->ntparams > 0;
    }
    bool structLitAhead() const {
        return is(T::LBrace) && peek(1).k == T::Ident && peek(2).k == T::Colon;
    }

    static ExprP mk(EK k, Pos p) {
        auto e = std::make_unique<Expr>();
        e->k = k;
        e->pos = p;
        return e;
    }

    void callArgs(Expr& e) {
        if (!is(T::RParen)) {
            do { e.args.push_back(parseExpr()); } while (accept(T::Comma));
        }
        expect(T::RParen, "`)`");
    }

    void structFields(Expr& lit) {
        take();  // {
        do {
            if (is(T::RBrace)) break;  // trailing comma
            Token fn = expect(T::Ident, "a field name");
            expect(T::Colon, "`:` after the field name");
            lit.names.push_back(fn.text);
            lit.args.push_back(parseExpr());
        } while (accept(T::Comma));
        expect(T::RBrace, "`}`");
    }

    ExprP parsePrimary() {
        Token tk = cur();
        switch (tk.k) {
            case T::Int: { take(); auto e = mk(EK::Int, tk.pos); e->ival = tk.ival; return e; }
            case T::Float: { take(); auto e = mk(EK::Float, tk.pos); e->fval = tk.fval; return e; }
            case T::Null: { take(); return mk(EK::Null, tk.pos); }
            case T::Char: { take(); auto e = mk(EK::Int, tk.pos); e->ival = tk.ival; return e; }
            case T::New: {  // new [T](n): a zeroed array of n elements (null / 0 / 0.0 / false / empty string)
                take();
                Pos tp = cur().pos;
                Ty at = parseType();
                if (!isArr(at)) throw CompileError{tp, "`new` needs an array type such as `new [int](n)`"};
                expect(T::LParen, "`(` and a size after the array type");
                auto e = mk(EK::Call, tk.pos);
                e->s = "new";
                e->slot = at;
                e->args.push_back(parseExpr());
                expect(T::RParen, "`)`");
                return e;
            }
            case T::True: case T::False: { take(); auto e = mk(EK::Bool, tk.pos); e->ival = tk.k == T::True; return e; }
            case T::Str: { take(); auto e = mk(EK::Str, tk.pos); e->s = tk.text; return e; }
            case T::LParen: {
                take();
                auto e = parseExpr();
                expect(T::RParen, "`)`");
                return e;
            }
            case T::Fn: {  // fn(x: int) -> int { ... }: an anonymous function (a closure if it uses outer variables)
                take();
                auto e = mk(EK::Lambda, tk.pos);
                auto f = std::make_shared<FnDecl>();
                f->pos = tk.pos;
                f->unit = unit;
                f->local = "fn";
                expect(T::LParen, "`(` and the parameters of the anonymous function");
                if (!is(T::RParen)) {
                    do {
                        Token pn = expect(T::Ident, "a parameter name");
                        expect(T::Colon, "`:` and a type after the parameter name (anonymous functions declare their parameter types)");
                        f->params.push_back(Param{pn.text, parseType(), pn.pos});
                    } while (accept(T::Comma));
                }
                expect(T::RParen, "`)`");
                if (accept(T::Arrow)) f->ret = parseType();
                f->body = parseBlock();
                e->fn = f;
                return e;
            }
            case T::LBrack: {  // array literal [a, b, c]
                take();
                auto e = mk(EK::ArrayLit, tk.pos);
                while (!is(T::RBrack)) {
                    e->args.push_back(parseExpr());
                    if (!accept(T::Comma)) break;
                }
                expect(T::RBrack, "`]` to close the array literal");
                return e;
            }
            case T::Ident: {
                take();
                std::string q, name = tk.text;
                if (isAlias(unit, tk.text) && is(T::Dot)) {  // module.name
                    take();
                    name = expect(T::Ident, "a name after `" + tk.text + ".`").text;
                    q = tk.text;
                }
                // expressions led by a type name: `Name { f: v }`, `Name<T> { ... }`, `Enum.Variant(...)`
                if (is(T::Dot) || is(T::Lt) || structLitAhead()) {
                    if (const Sym* ts = exprTypeSym(q, name, tk.pos)) {
                        int idx = structIndex(ts->mangled);
                        bool isE = structs()[idx].isEnum, gen = structs()[idx].isTemplate();
                        std::vector<Ty> targs;
                        bool has = false;
                        if (gen && is(T::Lt)) {
                            Pos ap = cur().pos;
                            targs = parseTypeArgs();
                            has = true;
                            if (targs.size() != structs()[idx].tparams.size())
                                throw CompileError{ap, "`" + name + "` takes " + std::to_string(structs()[idx].tparams.size()) +
                                                           " type argument(s), got " + std::to_string(targs.size())};
                        }
                        if (isE && is(T::Dot)) {
                            take();
                            Token v = expect(T::Ident, "a variant name after `" + name + ".`");
                            auto e = mk(EK::EnumCtor, tk.pos);
                            e->s = ts->mangled;
                            e->q = q;
                            e->names = {v.text};
                            e->targs = targs;
                            e->hasTargs = has;
                            if (accept(T::LParen)) callArgs(*e);
                            return e;
                        }
                        if (!isE && structLitAhead()) {
                            auto lit = mk(EK::StructLit, tk.pos);
                            lit->s = ts->mangled;
                            lit->targs = targs;
                            lit->hasTargs = has;
                            structFields(*lit);
                            return lit;
                        }
                        if (has) throw CompileError{cur().pos, std::string("expected ") + (isE ? "`.` and a variant" : "`{` and the fields") +
                                                                   " after `" + name + "<...>`"};
                    } else if (structLitAhead() && q.empty() && !tscope.count(name)) {
                        auto lit = mk(EK::StructLit, tk.pos);  // an unknown struct name: the checker reports it
                        lit->s = name;
                        structFields(*lit);
                        return lit;
                    }
                }
                std::vector<Ty> targs;
                bool has = false;
                if (is(T::Lt) && isGenericFn(q, name, tk.pos)) {  // f<int>(x), or f<int> as a function value
                    targs = parseTypeArgs();
                    has = true;
                }
                if (accept(T::LParen)) {
                    auto e = mk(EK::Call, tk.pos);
                    e->s = name;
                    e->q = q;
                    e->targs = targs;
                    e->hasTargs = has;
                    callArgs(*e);
                    return e;
                }
                auto e = mk(EK::Var, tk.pos);
                e->s = name;
                e->q = q;
                e->targs = targs;
                e->hasTargs = has;
                return e;
            }
            default:
                throw CompileError{tk.pos, "expected an expression, found " + describe(tk)};
        }
    }

    ExprP parsePostfix() {
        auto e = parsePrimary();
        while (is(T::LBrack) || is(T::Dot) || is(T::LParen)) {
            if (is(T::LParen)) {  // calling the value of an expression: f(1)(2), fs[0](x), (fn(...) { ... })(3)
                Token lp = take();
                auto c = mk(EK::CallInd, lp.pos);
                c->args.push_back(std::move(e));
                if (!is(T::RParen)) {
                    do { c->args.push_back(parseExpr()); } while (accept(T::Comma));
                }
                expect(T::RParen, "`)`");
                e = std::move(c);
                continue;
            }
            if (is(T::Dot)) {
                take();
                Token f = expect(T::Ident, "a field name after `.`");
                auto fe = mk(EK::Field, f.pos);
                fe->s = f.text;
                fe->args.push_back(std::move(e));
                e = std::move(fe);
                continue;
            }
            Token lb = take();
            auto idx = mk(EK::Index, lb.pos);
            idx->args.push_back(std::move(e));
            idx->args.push_back(parseExpr());
            expect(T::RBrack, "`]`");
            e = std::move(idx);
        }
        return e;
    }

    ExprP parseUnary() {
        if (is(T::Minus) || is(T::Bang) || is(T::Tilde)) {
            Token op = take();
            auto e = mk(EK::Unary, op.pos);
            e->op = op.k;
            e->args.push_back(parseUnary());
            return e;
        }
        return parsePostfix();
    }

    static int prec(T k) {
        switch (k) {
            case T::OrOr: return 1;
            case T::AndAnd: return 2;
            // bitwise operators bind tighter than comparisons (as in Rust and Go), so `x & 1 == 1` means `(x & 1) == 1`
            case T::Eq: case T::Ne: return 3;
            case T::Lt: case T::Le: case T::Gt: case T::Ge: return 4;
            case T::Pipe: return 5;
            case T::Caret: return 6;
            case T::Amp: return 7;
            case T::Shl: case T::Shr: return 8;
            case T::Plus: case T::Minus: return 9;
            case T::Star: case T::Slash: case T::Percent: return 10;
            default: return 0;
        }
    }

    ExprP parseBin(int minPrec) {
        auto lhs = parseUnary();
        while (true) {
            int p = prec(cur().k);
            if (p == 0 || p < minPrec) break;
            Token op = take();
            auto rhs = parseBin(p + 1);
            auto e = mk(EK::Binary, op.pos);
            e->op = op.k;
            e->args.push_back(std::move(lhs));
            e->args.push_back(std::move(rhs));
            lhs = std::move(e);
        }
        return lhs;
    }

    ExprP parseExpr() { return parseBin(1); }

    static bool hasCall(const Expr& e) {
        if (e.k == EK::Call || e.k == EK::StructLit || e.k == EK::EnumCtor || e.k == EK::ArrayLit || e.k == EK::CallInd ||
            e.k == EK::Lambda || e.k == EK::FnRef)
            return true;
        for (auto& a : e.args)
            if (hasCall(*a)) return true;
        return false;
    }
    static bool compoundOp(T k, T& bin) {
        switch (k) {
            case T::PlusEq: bin = T::Plus; return true;
            case T::MinusEq: bin = T::Minus; return true;
            case T::StarEq: bin = T::Star; return true;
            case T::SlashEq: bin = T::Slash; return true;
            case T::PercentEq: bin = T::Percent; return true;
            case T::AmpEq: bin = T::Amp; return true;
            case T::PipeEq: bin = T::Pipe; return true;
            case T::CaretEq: bin = T::Caret; return true;
            case T::ShlEq: bin = T::Shl; return true;
            case T::ShrEq: bin = T::Shr; return true;
            default: return false;
        }
    }

    static StmtP mks(SK k, Pos p) {
        auto s = std::make_unique<Stmt>();
        s->k = k;
        s->pos = p;
        return s;
    }

    std::vector<StmtP> parseBlock() {
        expect(T::LBrace, "`{`");
        std::vector<StmtP> out;
        while (!is(T::RBrace)) {
            if (is(T::End)) throw CompileError{cur().pos, "expected `}` before end of file"};
            out.push_back(parseStmt());
        }
        take();
        return out;
    }

    StmtP parseIf() {
        Token kw = expect(T::If, "`if`");
        auto s = mks(SK::If, kw.pos);
        s->e1 = parseExpr();
        s->body = parseBlock();
        if (accept(T::Else)) {
            if (is(T::If)) s->els.push_back(parseIf());
            else s->els = parseBlock();
        }
        return s;
    }

    // match value { Variant(a, b) => { ... } Other => { ... } _ => { ... } }
    StmtP parseMatch() {
        Token kw = expect(T::Match, "`match`");
        auto s = mks(SK::Match, kw.pos);
        s->e1 = parseExpr();
        expect(T::LBrace, "`{` after the value to match");
        while (!is(T::RBrace)) {
            if (is(T::End)) throw CompileError{cur().pos, "expected `}` before end of file"};
            MatchArm a;
            Token v = expect(T::Ident, "a variant name or `_`");
            a.variant = v.text;
            a.pos = v.pos;
            if (accept(T::LParen)) {
                if (!is(T::RParen)) {
                    do {
                        Token b = expect(T::Ident, "a name to bind the payload value to (or `_`)");
                        a.binds.push_back(b.text);
                        a.bpos.push_back(b.pos);
                    } while (accept(T::Comma));
                }
                expect(T::RParen, "`)`");
            }
            expect(T::FatArrow, "`=>`");
            a.body = parseBlock();
            accept(T::Comma);
            s->arms.push_back(std::move(a));
        }
        take();
        return s;
    }

    StmtP parseStmt() {
        Token tk = cur();
        switch (tk.k) {
            case T::Let: {
                take();
                auto s = mks(SK::Let, tk.pos);
                s->mut = accept(T::Mut);
                Token name = expect(T::Ident, "a variable name");
                s->name = name.text;
                s->pos = name.pos;
                if (accept(T::Colon)) {
                    s->hasTy = true;
                    s->declTy = parseType();
                }
                expect(T::Assign, "`=`");
                s->e1 = parseExpr();
                expect(T::Semi, "`;`");
                return s;
            }
            case T::If: return parseIf();
            case T::Match: return parseMatch();
            case T::While: {
                take();
                auto s = mks(SK::While, tk.pos);
                s->e1 = parseExpr();
                s->body = parseBlock();
                return s;
            }
            case T::For: {
                take();
                auto s = mks(SK::For, tk.pos);
                Token name = expect(T::Ident, "a loop variable name");
                s->name = name.text;
                s->pos = name.pos;
                expect(T::In, "`in`");
                s->e1 = parseExpr();
                expect(T::DotDot, "`..`");
                s->e2 = parseExpr();
                s->body = parseBlock();
                return s;
            }
            case T::Return: {
                take();
                auto s = mks(SK::Return, tk.pos);
                if (!is(T::Semi)) s->e1 = parseExpr();
                expect(T::Semi, "`;`");
                return s;
            }
            case T::Break: take(); expect(T::Semi, "`;`"); return mks(SK::Break, tk.pos);
            case T::Continue: take(); expect(T::Semi, "`;`"); return mks(SK::Continue, tk.pos);
            case T::LBrace: {
                auto s = mks(SK::Block, tk.pos);
                s->body = parseBlock();
                return s;
            }
            default: {
                auto e = parseExpr();
                T bop;
                if (compoundOp(cur().k, bop)) {  // `x op= v`  is  `x = x op v`
                    Token eq = take();
                    if (e->k != EK::Var && e->k != EK::Index && e->k != EK::Field)
                        throw CompileError{eq.pos, "the left side of `" + eq.text + "` must be a variable, an array element or a struct field"};
                    if (hasCall(*e)) throw CompileError{eq.pos, "the target of `" + eq.text + "` must not contain a call (it would run twice); write `x = x op v`"};
                    auto s = mks(SK::Assign, eq.pos);
                    auto b = mk(EK::Binary, eq.pos);
                    b->op = bop;
                    b->args.push_back(cloneExpr(*e));
                    b->args.push_back(parseExpr());
                    s->e1 = std::move(e);
                    s->e2 = std::move(b);
                    expect(T::Semi, "`;`");
                    return s;
                }
                if (is(T::Assign)) {
                    Token eq = take();
                    if (e->k != EK::Var && e->k != EK::Index && e->k != EK::Field)
                        throw CompileError{eq.pos, "the left side of `=` must be a variable, an array element or a struct field"};
                    auto s = mks(SK::Assign, eq.pos);
                    s->e1 = std::move(e);
                    s->e2 = parseExpr();
                    expect(T::Semi, "`;`");
                    return s;
                }
                auto s = mks(SK::ExprS, e->pos);
                s->e1 = std::move(e);
                expect(T::Semi, "`;`");
                return s;
            }
        }
    }

    // `<T, U>` after a declaration name: fresh parameter types, put in scope
    std::vector<Ty> declTypeParams() {
        std::vector<Ty> ps;
        if (!accept(T::Lt)) return ps;
        do {
            Token p = expect(T::Ident, "a type parameter name");
            if (tscope.count(p.text)) throw CompileError{p.pos, "type parameter `" + p.text + "` is declared twice"};
            Ty pt = types().newParam(p.text);
            tscope[p.text] = pt;
            ps.push_back(pt);
        } while (accept(T::Comma));
        closeAngle();
        return ps;
    }

    FnDecl parseFn() {
        expect(T::Fn, "`fn`");
        FnDecl f;
        Token name = expect(T::Ident, "a function name");
        f.local = name.text;
        f.name = mangle(unit, name.text);
        f.unit = unit;
        f.pos = name.pos;
        tscope.clear();
        f.tparams = declTypeParams();
        expect(T::LParen, "`(`");
        if (!is(T::RParen)) {
            do {
                Token pn = expect(T::Ident, "a parameter name");
                expect(T::Colon, "`:` and a type after the parameter name");
                f.params.push_back(Param{pn.text, parseType(), pn.pos});
            } while (accept(T::Comma));
        }
        expect(T::RParen, "`)`");
        if (accept(T::Arrow)) f.ret = parseType();
        f.body = parseBlock();
        tscope.clear();
        return f;
    }

    // re-enter the type parameters a pre-scanned struct / enum declared, skipping `<T, U>` in the source
    void enterTemplate(int idx) {
        tscope.clear();
        if (!accept(T::Lt)) return;
        while (!is(T::Gt) && !is(T::End)) take();
        closeAngle();
        for (Ty p : structs()[idx].tparams) tscope[tyName(p)] = p;
    }

public:
    explicit Parser(std::vector<Token> toks, int unitId = 0) : t(std::move(toks)), unit(unitId) {
        // register type names up front so types may refer to structs and enums declared later (and to themselves)
        for (size_t k = 0; k + 1 < t.size(); k++) {
            if ((t[k].k == T::Struct || t[k].k == T::Enum) && t[k + 1].k == T::Ident) {
                bool pub = k > 0 && t[k - 1].k == T::Pub;
                bool isE = t[k].k == T::Enum;
                std::vector<Ty> tps;
                std::set<std::string> seen;
                if (k + 2 < t.size() && t[k + 2].k == T::Lt)
                    for (size_t j = k + 3; j < t.size() && t[j].k != T::Gt; j++)
                        if (t[j].k == T::Ident) {
                            if (!seen.insert(t[j].text).second)
                                throw CompileError{t[j].pos, "type parameter `" + t[j].text + "` is declared twice"};
                            tps.push_back(types().newParam(t[j].text));
                        }
                CompileError ce;
                if (!declareSym(unit, SymK::Struct, t[k + 1].text, pub, t[k + 1].pos, ce, isE, (int)tps.size())) throw ce;
                types().declareStruct(mangle(unit, t[k + 1].text), t[k + 1].pos, t[k + 1].text, unit, pub, isE, tps);
            }
            if (t[k].k == T::Fn && t[k + 1].k == T::Ident) {
                ownFns.insert(t[k + 1].text);
                if (k + 2 < t.size() && t[k + 2].k == T::Lt) genericFns.insert(t[k + 1].text);
            }
        }
    }

    Program parseProgram() {
        Program p;
        while (!is(T::End)) {
            if (is(T::Import)) {  // resolved by the driver before parsing; here it is only checked and skipped
                take();
                Token path = expect(T::Str, "a module path in quotes");
                std::string alias;
                if (accept(T::As)) alias = expect(T::Ident, "a module name after `as`").text;
                expect(T::Semi, "`;`");
                p.imports.push_back({path.text, alias, path.pos});
                continue;
            }
            Token pubTok = cur();
            bool pub = accept(T::Pub);
            if (pub && !is(T::Fn) && !is(T::Struct) && !is(T::Const) && !is(T::Enum))
                throw CompileError{pubTok.pos, "`pub` must be followed by `fn`, `struct`, `enum` or `const`"};
            if (is(T::Const)) {
                take();
                ConstDecl c;
                Token name = expect(T::Ident, "a constant name");
                c.local = name.text;
                c.name = mangle(unit, name.text);
                c.unit = unit;
                c.pub = pub;
                c.pos = name.pos;
                {
                    CompileError ce;
                    if (!declareSym(unit, SymK::Const, name.text, pub, name.pos, ce)) throw ce;
                }
                bool hasTy = false;
                Ty declTy = TyErr;
                if (accept(T::Colon)) { hasTy = true; declTy = parseType(); }
                expect(T::Assign, "`=`");
                bool neg = accept(T::Minus);
                Token v = cur();
                if (v.k == T::Int || v.k == T::Char) { take(); c.ty = TyInt; c.ival = neg ? -v.ival : v.ival; }
                else if (v.k == T::Float) { take(); c.ty = TyFloat; c.fval = neg ? -v.fval : v.fval; }
                else if (!neg && (v.k == T::True || v.k == T::False)) { take(); c.ty = TyBool; c.ival = v.k == T::True; }
                else if (!neg && v.k == T::Str) { take(); c.ty = TyStr; c.sval = v.text; }
                else throw CompileError{v.pos, "a constant must be a number, bool, char or string literal, found " + describe(v)};
                if (hasTy && declTy != c.ty)
                    throw CompileError{v.pos, std::string("expected `") + tyName(declTy) + "`, found `" + tyName(c.ty) + "`"};
                expect(T::Semi, "`;`");
                p.consts.push_back(c);
                continue;
            }
            if (is(T::Struct)) {
                take();
                Token sn = expect(T::Ident, "a struct name");
                int idx = structIndex(mangle(unit, sn.text));
                enterTemplate(idx);
                expect(T::LBrace, "`{`");
                while (!is(T::RBrace)) {
                    Token fname = expect(T::Ident, "a field name");
                    expect(T::Colon, "`:` and a type after the field name");
                    Ty ft = parseType();  // (may add instances to the struct table: index, do not hold references)
                    structs()[idx].fnames.push_back(fname.text);
                    structs()[idx].ftys.push_back(ft);
                    structs()[idx].fpos.push_back(fname.pos);
                    if (!accept(T::Comma)) break;
                }
                expect(T::RBrace, "`}`");
                tscope.clear();
                types().completeTemplate(idx);
                continue;
            }
            if (is(T::Enum)) {  // enum Name<T> { A(T, int), B, }
                take();
                Token en = expect(T::Ident, "an enum name");
                int idx = structIndex(mangle(unit, en.text));
                enterTemplate(idx);
                expect(T::LBrace, "`{`");
                while (!is(T::RBrace)) {
                    Token vn = expect(T::Ident, "a variant name");
                    std::vector<Ty> pay;
                    if (accept(T::LParen)) {
                        if (!is(T::RParen)) {
                            do { pay.push_back(parseType()); } while (accept(T::Comma));
                        }
                        expect(T::RParen, "`)`");
                    }
                    structs()[idx].vnames.push_back(vn.text);
                    structs()[idx].vtys.push_back(pay);
                    structs()[idx].vpos.push_back(vn.pos);
                    if (!accept(T::Comma)) break;
                }
                expect(T::RBrace, "`}`");
                tscope.clear();
                types().completeTemplate(idx);
                continue;
            }
            if (!is(T::Fn)) throw CompileError{cur().pos, "expected `fn`, `struct`, `enum`, `const` or `import` at top level, found " + describe(cur())};
            p.funcs.push_back(parseFn());
            p.funcs.back().pub = pub;
            {
                CompileError ce;
                if (!declareSym(unit, SymK::Fn, p.funcs.back().local, pub, p.funcs.back().pos, ce, false,
                                (int)p.funcs.back().tparams.size()))
                    throw ce;
            }
        }
        return p;
    }
};

// mono.hpp - monomorphization: one copy of each generic function per list of type arguments it is used with.
//
// The checker has already typed every generic body with its parameters opaque, and recorded the (inferred) type
// arguments of every call to a generic function. Starting from the non-generic functions, each such call is renamed
// to its instance (`vec.push<int>`) and the instance is made by deep-copying the template and substituting the type
// arguments into every type in it: expression types, declared types, `new [T](n)`, nested calls' type arguments.
// Nothing after this point (IR, optimizer, interpreter, code generator) knows that generics exist.
#pragma once
#include <deque>
#include <set>

#include "ast.hpp"

class Mono {
    std::map<std::string, size_t> templ;  // generic function (mangled name) -> index in the template list
    std::vector<FnDecl> templates;
    std::set<std::string> made;
    struct Job { std::string name; size_t t; std::vector<Ty> targs; };
    std::deque<Job> work;

    static std::string instName(const std::string& fn, const std::vector<Ty>& ta) {
        std::string n = fn + "<";
        for (size_t i = 0; i < ta.size(); i++) n += (i ? ", " : "") + tyName(ta[i]);
        return n + ">";
    }

    static void substExpr(Expr& e, const std::map<Ty, Ty>& m) {
        e.ty = types().subst(e.ty, m);
        if (e.k == EK::Call && e.s == "new") e.slot = types().subst(e.slot, m);
        for (Ty& t : e.targs) t = types().subst(t, m);
        for (auto& a : e.args) substExpr(*a, m);
        if (e.fn) {  // an anonymous function inside a generic one
            for (Param& pr : e.fn->params) pr.ty = types().subst(pr.ty, m);
            e.fn->ret = types().subst(e.fn->ret, m);
            for (auto& c : e.fn->caps) c.ty = types().subst(c.ty, m);
            substBlock(e.fn->body, m);
        }
    }
    static void substBlock(std::vector<StmtP>& b, const std::map<Ty, Ty>& m) {
        for (auto& s : b) {
            s->declTy = types().subst(s->declTy, m);
            if (s->e1) substExpr(*s->e1, m);
            if (s->e2) substExpr(*s->e2, m);
            substBlock(s->body, m);
            substBlock(s->els, m);
            for (auto& a : s->arms) substBlock(a.body, m);
        }
    }

    void renameExpr(Expr& e) {
        if ((e.k == EK::Call || e.k == EK::FnRef) && !e.targs.empty()) {
            auto it = templ.find(e.s);
            if (it != templ.end()) {
                std::string n = instName(e.s, e.targs);
                if (made.insert(n).second) work.push_back({n, it->second, e.targs});
                e.s = n;
            }
        }
        for (auto& a : e.args) renameExpr(*a);
        if (e.fn) renameBlock(e.fn->body);
    }
    void renameBlock(std::vector<StmtP>& b) {
        for (auto& s : b) {
            if (s->e1) renameExpr(*s->e1);
            if (s->e2) renameExpr(*s->e2);
            renameBlock(s->body);
            renameBlock(s->els);
            for (auto& a : s->arms) renameBlock(a.body);
        }
    }

public:
    // Replaces p.funcs by the non-generic functions plus every instance that is used.
    void run(Program& p) {
        std::vector<FnDecl> out;
        for (auto& f : p.funcs) {
            if (f.tparams.empty()) out.push_back(std::move(f));
            else {
                templ[f.name] = templates.size();
                templates.push_back(std::move(f));
            }
        }
        for (auto& f : out) renameBlock(f.body);
        while (!work.empty()) {
            Job j = work.front();
            work.pop_front();
            const FnDecl& tp = templates[j.t];
            if (made.size() > 5000)
                throw CompileError{tp.pos, "generic function `" + tp.local + "` is instantiated without end (it calls itself "
                                                 "with ever larger type arguments)"};
            std::map<Ty, Ty> m;
            for (size_t i = 0; i < tp.tparams.size(); i++) m[tp.tparams[i]] = j.targs[i];
            FnDecl c;
            c.name = j.name;
            c.local = tp.local;
            c.unit = tp.unit;
            c.pub = tp.pub;
            c.pos = tp.pos;
            c.nvars = tp.nvars;
            for (const Param& pr : tp.params) c.params.push_back(Param{pr.name, types().subst(pr.ty, m), pr.pos});
            c.ret = types().subst(tp.ret, m);
            c.body = cloneBlock(tp.body);
            substBlock(c.body, m);
            renameBlock(c.body);
            out.push_back(std::move(c));
        }
        p.funcs = std::move(out);
    }
};

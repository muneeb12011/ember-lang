// embc - the Ember compiler driver.
//
//   embc prog.em                 compile to ./a.out (native x86-64 via the system assembler/linker)
//   embc prog.em -o prog -O1     choose output file and optimisation level (default -O1)
//   embc prog.em --run           compile, then run
//   embc prog.em --interp        run on the built-in IR interpreter (no gcc needed)
//   embc prog.em --emit-ir       print the (optimised) IR
//   embc prog.em --emit-asm      print the generated assembly
//   embc prog.em --stats         print optimiser statistics
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>

#include <algorithm>
#include <climits>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include "codegen.hpp"
#include "fmt.hpp"
#include "interp.hpp"
#include "ir.hpp"
#include "lexer.hpp"
#include "mono.hpp"
#include "opt.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include "toolchain.hpp"
#include "wasm.hpp"

// ------------------------------------------------------------------------------------- modules
// `import "path";` / `import "path" as name;` pull in another file. "std/x.em" comes from the standard library embedded
// in this binary; any other path is relative to the importing file. Each file is loaded once (diamond imports are fine)
// and becomes one Unit (see modules.hpp); imports are loaded before the importer is parsed (so the importer can use
// their structs), and a cycle is an error. Importing a standard module without `as` also brings in its compatibility
// file (std/compat/x.em), which keeps the old prefixed names (math_abs, vec_push, ...) working for one release.
struct Loader {
    std::map<std::string, int> done;  // file key -> unit
    std::set<std::string> active;
    std::vector<std::string> chain;
    std::map<std::string, int> stems;
    Program out;

    static std::string dirOf(const std::string& p) {
        size_t k = p.rfind('/');
        return k == std::string::npos ? std::string() : p.substr(0, k + 1);
    }
    static const char* embedded(const std::string& path) {
        for (const EmbeddedFile* f = kStdlib; f->name; ++f)
            if (path == f->name) return f->text;
        return nullptr;
    }

    int newUnit(const std::string& display, bool root) {
        Unit u;
        u.path = display;
        if (!root) {
            std::string stem;
            if (display.rfind("std/", 0) == 0) {  // std/math.em -> math. ; std/compat/math.em -> compat.math.
                stem = display.substr(4, display.size() - 7);
                for (char& c : stem) if (c == '/') c = '.';
                u.compat = display.rfind("std/compat/", 0) == 0;
            } else {
                size_t sl = display.rfind('/');
                stem = display.substr(sl == std::string::npos ? 0 : sl + 1);
                if (stem.size() > 3 && stem.compare(stem.size() - 3, 3, ".em") == 0) stem.resize(stem.size() - 3);
                for (char& c : stem)
                    if (!isalnum((unsigned char)c) && c != '_') c = '_';
            }
            int n = stems[stem]++;
            u.prefix = stem + (n ? std::to_string(n + 1) : "") + ".";
        }
        units().push_back(u);
        return (int)units().size() - 1;
    }

    int loadSource(const std::string& key, const std::string& display, const std::string& baseDir, const std::string& text,
                   bool root = false) {
        active.insert(key);
        chain.push_back(display);
        int fid = addSource(display, text);
        int uid = newUnit(display, root);
        std::vector<Token> toks = lex(text, fid);
        for (size_t i = 0; i + 1 < toks.size(); i++) {
            if (toks[i].k != T::Import || toks[i + 1].k != T::Str) continue;
            const std::string& path = toks[i + 1].text;
            int dep = resolve(path, toks[i + 1].pos, baseDir);
            if (i + 3 < toks.size() && toks[i + 2].k == T::As && toks[i + 3].k == T::Ident) {
                const Token& al = toks[i + 3];
                Unit& u = units()[uid];
                if (u.aliases.count(al.text)) {
                    CompileError e{al.pos, "module name `" + al.text + "` is already used by another import"};
                    e.notePos = u.aliasPos[al.text];
                    e.note = "first used here";
                    throw e;
                }
                u.aliases[al.text] = dep;
                u.aliasPos[al.text] = al.pos;
            } else {
                auto addGlob = [&](int d) {
                    auto& g = units()[uid].globs;
                    if (std::find(g.begin(), g.end(), d) == g.end()) g.push_back(d);
                };
                addGlob(dep);
                if (path.rfind("std/", 0) == 0 && path.rfind("std/compat/", 0) != 0) {
                    std::string cp = "std/compat/" + path.substr(4);
                    if (embedded(cp)) addGlob(resolve(cp, toks[i + 1].pos, baseDir));
                }
            }
        }
        Program p = Parser(std::move(toks), uid).parseProgram();
        for (auto& f : p.funcs) out.funcs.push_back(std::move(f));
        for (auto& c : p.consts) out.consts.push_back(std::move(c));
        chain.pop_back();
        active.erase(key);
        done[key] = uid;
        return uid;
    }

    int resolve(const std::string& path, Pos pos, const std::string& baseDir) {
        std::string key, text, nbase;
        if (path.rfind("std/", 0) == 0) {
            const char* t = embedded(path);
            if (!t) throw CompileError{pos, "there is no standard library module `" + path + "`"};
            text = t;
            key = path;
        } else {
            std::string full = (!path.empty() && path[0] == '/') ? path : baseDir + path;
            char* rp = realpath(full.c_str(), nullptr);
            if (!rp) throw CompileError{pos, "cannot find module `" + path + "` (looked for " + full + ")"};
            key = rp;
            free(rp);
            std::ifstream fh(key, std::ios::binary);
            std::stringstream b;
            b << fh.rdbuf();
            text = b.str();
            nbase = dirOf(key);
        }
        auto d = done.find(key);
        if (d != done.end()) return d->second;
        if (active.count(key)) {
            std::string c;
            for (const std::string& n : chain) c += n + " -> ";
            throw CompileError{pos, "import cycle: " + c + path};
        }
        return loadSource(key, path.rfind("std/", 0) == 0 ? path : baseDir + path, nbase, text);
    }
};

static void usage() {
    std::cerr << "usage: embc <file.em> [-o out] [-O0|-O1] [--run] [--interp] [--emit-ir] [--emit-asm] [--stats] [--keep-asm] [-g] [-- program args]\n"
                 "       embc <file.em> --target wasm [-o out.js] [--run]   WebAssembly (an executable JS file for node)\n"
                 "       embc <file.em> --test [--interp]      run every `fn test_*()` in the file, each on its own\n"
                 "       embc fmt <file.em> [-w] [--check]     format (print, -w: rewrite the file, --check: exit 1 if it would change)\n";
}

static int formatCommand(int argc, char** argv) {
    std::string in;
    bool write = false, checkOnly = false;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-w" || a == "--write") write = true;
        else if (a == "--check") checkOnly = true;
        else in = a;
    }
    if (in.empty()) { usage(); return 2; }
    std::ifstream fh(in, std::ios::binary);
    if (!fh) { std::cerr << "embc: cannot read " << in << "\n"; return 2; }
    std::stringstream buf;
    buf << fh.rdbuf();
    std::string src = buf.str(), res;
    addSource(in, src);
    try {
        res = Formatter(src).run();
    } catch (const CompileError& e) {
        printError(e);
        return 1;
    }
    if (checkOnly) return res == src ? 0 : 1;
    if (write) {
        if (res != src) {
            std::ofstream o(in, std::ios::binary);
            o << res;
        }
        return 0;
    }
    std::cout << res;
    return 0;
}

// Run `fn` with stdout and stderr captured; returns the exit status.
template <class F>
static int captured(F fn, std::string& output) {
    int fds[2];
    if (pipe(fds) != 0) return 127;
    std::fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        int rc = fn();
        std::fflush(stdout);
        _exit(rc);
    }
    close(fds[1]);
    char b[4096];
    ssize_t n;
    while ((n = read(fds[0], b, sizeof b)) > 0) output.append(b, (size_t)n);
    close(fds[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
}

static long long runInterp(const Module& mod, const std::vector<std::string>& progArgs);
static int linkAndRun(const std::string& asmText, const std::string& out, bool keepAsm, bool run,
                      const std::vector<std::string>& progArgs);

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "fmt") return formatCommand(argc, argv);
    std::string in, out = "a.out";
    std::vector<std::string> progArgs;  // everything after a lone `--` goes to the program being run
    int level = 1;
    bool run = false, interp = false, emitIR = false, emitAsm = false, stats = false, keepAsm = false, debug = false, test = false;
    std::string target = "x86-64";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--") { for (int j = i + 1; j < argc; j++) progArgs.push_back(argv[j]); break; }
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a == "-O0") level = 0;
        else if (a == "-O1" || a == "-O2") level = 1;
        else if (a == "--run") run = true;
        else if (a == "--interp") interp = true;
        else if (a == "--emit-ir") emitIR = true;
        else if (a == "--emit-asm") emitAsm = true;
        else if (a == "--stats") stats = true;
        else if (a == "--keep-asm") keepAsm = true;
        else if (a == "-g") debug = true;
        else if (a == "--test") test = true;
        else if (a == "--target" && i + 1 < argc) target = argv[++i];
        else if (a.rfind("--target=", 0) == 0) target = a.substr(9);
        else if (!a.empty() && a[0] == '-') { usage(); return 2; }
        else in = a;
    }
    if (in.empty()) { usage(); return 2; }

    std::ifstream fh(in, std::ios::binary);
    if (!fh) { std::cerr << "embc: cannot read " << in << "\n"; return 2; }
    std::stringstream buf;
    buf << fh.rdbuf();
    std::string src = buf.str();

    Program prog;
    try {
        Loader ld;
        char* rp = realpath(in.c_str(), nullptr);
        std::string key = rp ? rp : in;
        free(rp);
        ld.loadSource(key, in, Loader::dirOf(in), src, true);
        prog = std::move(ld.out);
    } catch (const CompileError& e) {
        printError(e);
        return 1;
    }
    std::vector<std::string> tests;  // --test: a generated main runs test number arg(0)
    if (test) {
        for (auto& f : prog.funcs)
            if (f.unit == 0 && f.local.rfind("test_", 0) == 0) tests.push_back(f.local);
        for (auto& f : prog.funcs)
            if (f.unit == 0 && f.name == "main") {  // the file's own main steps aside
                f.name = f.local = "main$replaced";
                units()[0].syms.erase({(int)SymK::Fn, "main"});
            }
        std::string h = "fn main() -> int {\n    let k = parse_int(arg(0));\n";
        for (size_t i = 0; i < tests.size(); i++) h += "    if k == " + std::to_string(i) + " { " + tests[i] + "(); }\n";
        h += "    return 0;\n}\n";
        try {
            Program hp = Parser(lex(h, addSource("<test harness>", h)), 0).parseProgram();
            for (auto& f : hp.funcs) prog.funcs.push_back(std::move(f));
        } catch (const CompileError& e) {
            printError(e);
            return 1;
        }
    }
    Sema sema;
    auto errs = sema.run(prog);
    if (test && errs.empty())
        for (auto& f : prog.funcs)
            if (f.unit == 0 && f.local.rfind("test_", 0) == 0 && !f.params.empty())
                errs.push_back({f.pos, "a test function takes no parameters: `fn " + f.local + "()`"});
    for (auto& w : sema.warnings()) printDiag(w.pos, w.msg, "warning");
    if (!errs.empty()) {
        for (auto& e : errs) printError(e);
        std::cerr << errs.size() << " error" << (errs.size() == 1 ? "" : "s") << "\n";
        return 1;
    }

    try {
        Mono().run(prog);
    } catch (const CompileError& e) {
        printError(e);
        return 1;
    }
    Module mod = Lowerer().lower(prog);
    int before = countInsts(mod);
    opt::optimize(mod, level);
    int after = countInsts(mod);
    if (stats)
        std::cerr << "ir instructions: " << before << " -> " << after << " (-O" << level << "), " << opt::inlinedCount()
                  << " call sites inlined, " << mod.funcs.size() << " functions kept\n";

    if (emitIR) {
        for (auto& f : mod.funcs) std::cout << fmtFunc(f) << "\n";
        return 0;
    }

    if (test) {
        std::string exe;
        if (!interp) {  // build once, run once per test
            exe = (out == "a.out" ? std::string("/tmp/embc-test-") + std::to_string((long)getpid()) : out);
            std::string asmPath = exe + ".s";
            {
                std::ofstream af(asmPath);
                af << CodeGen(mod, in, debug).emit();
            }
            std::string lerr;
            int rc = linkProgram(asmPath, exe, lerr);
            std::remove(asmPath.c_str());
            if (rc != 0) {
                std::cerr << "embc: " << lerr << "\n";
                return 3;
            }
        }
        std::cout << "running " << tests.size() << " test" << (tests.size() == 1 ? "" : "s") << " from " << in << "\n";
        int failed = 0;
        for (size_t i = 0; i < tests.size(); i++) {
            std::string output;
            std::vector<std::string> args{std::to_string(i)};
            int rc = captured([&]() -> int {
                if (interp) return (int)runInterp(mod, args);
                execl(exe.c_str(), exe.c_str(), args[0].c_str(), (char*)nullptr);
                return 127;
            }, output);
            if (rc == 0) {
                std::cout << "test " << tests[i] << " ... ok\n";
            } else {
                failed++;
                std::cout << "test " << tests[i] << " ... FAILED\n";
                std::istringstream ls(output);
                std::string ln;
                while (std::getline(ls, ln)) std::cout << "    " << ln << "\n";
            }
        }
        if (!interp && out == "a.out") std::remove(exe.c_str());
        std::cout << tests.size() << " test" << (tests.size() == 1 ? "" : "s") << ": " << tests.size() - failed << " passed, " << failed
                  << " failed\n";
        return failed ? 1 : 0;
    }

    if (interp) return (int)runInterp(mod, progArgs);
    if (target == "wasm") {  // one executable JS file for node: the runtime module, this program's module, a loader
        std::vector<uint8_t> bytes;
        std::string why = WasmGen(mod).emit(bytes);
        if (!why.empty()) { std::cerr << "embc: " << why << "\n"; return 3; }
        if (out == "a.out") out = "a.out.js";
        std::string lerr;
        if (writeWasmProgram(bytes, out, lerr) != 0) { std::cerr << "embc: " << lerr << "\n"; return 3; }
        if (run) {
            std::string cmd = "node " + shq(out);
            for (const std::string& a : progArgs) cmd += " " + shq(a);
            int st = std::system(cmd.c_str());
            return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
        }
        return 0;
    }
    if (target != "x86-64") { std::cerr << "embc: unknown target `" << target << "` (x86-64 or wasm)\n"; return 2; }
    std::string asmText = CodeGen(mod, in, debug).emit();
    if (emitAsm) {
        std::cout << asmText;
        return 0;
    }
    return linkAndRun(asmText, out, keepAsm, run, progArgs);
}

static long long runInterp(const Module& mod, const std::vector<std::string>& progArgs) {
    {
        // The interpreter recurses on the C++ stack, so give it a big (lazily committed) one.
        struct Job { const Module* m; const std::vector<std::string>* args; int rc; } job{&mod, &progArgs, 0};
        auto body = [](void* p) -> void* {
            Job* j = static_cast<Job*>(p);
            try {
                long long r = Interp(*j->m, *j->args).runMain();
                std::fflush(stdout);
                j->rc = (int)(r & 255);
            } catch (const RuntimeError& e) {
                std::fflush(stdout);
                std::cerr << "runtime error: " << e.msg << " (line " << e.line << ")\n";
                j->rc = 1;
            } catch (const ExitRequest& x) {
                std::fflush(stdout);
                j->rc = x.code;
            }
            return nullptr;
        };
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, (size_t)1 << 30);
        pthread_t th;
        if (pthread_create(&th, &attr, body, &job) == 0) pthread_join(th, nullptr);
        else body(&job);
        return job.rc;
    }
}

static int linkAndRun(const std::string& asmText, const std::string& out, bool keepAsm, bool run,
                      const std::vector<std::string>& progArgs) {

    std::string asmPath = out + ".s";
    {
        std::ofstream af(asmPath);
        af << asmText;
    }
    std::string lerr;
    int rc = linkProgram(asmPath, out, lerr);
    if (!keepAsm) std::remove(asmPath.c_str());
    if (rc != 0) {
        std::cerr << "embc: " << lerr << "\n";
        return 3;
    }
    if (run) {
        std::string path = out.find('/') == std::string::npos ? "./" + out : out;
        std::string cmd = shq(path);
        for (const std::string& a : progArgs) cmd += " " + shq(a);
        int st = std::system(cmd.c_str());
        return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
    }
    return 0;
}

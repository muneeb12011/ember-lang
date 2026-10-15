// web_main.cpp - the Ember compiler built for the browser playground.
//
// The whole compiler (src/main.cpp) is compiled to WebAssembly with Emscripten. Its `main` is renamed so this file
// can call it with an argument list built from JavaScript: embc_run("main.em\n--interp") runs exactly what
// `embc main.em --interp` runs on Linux. Only the paths that need no external tools work here: --interp, --emit-ir,
// --emit-asm (gcc, node and fork() do not exist in a browser).
#define main embc_real_main
#include "../../src/main.cpp"
#undef main

#include <emscripten.h>

extern "C" EMSCRIPTEN_KEEPALIVE int embc_run(const char* argsJoined) {
    std::vector<std::string> args{"embc"};
    std::string cur;
    for (const char* p = argsJoined; *p; p++) {
        if (*p == '\n') { args.push_back(cur); cur.clear(); }
        else cur += *p;
    }
    if (!cur.empty()) args.push_back(cur);
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    int rc;
    try {
        rc = embc_real_main((int)args.size(), argv.data());
    } catch (const std::bad_alloc&) {
        std::cout.flush();
        std::cerr << "runtime error: out of memory (the browser limits a page's memory)\n";
        rc = 1;
    } catch (const std::exception& e) {
        std::cerr << "internal error: " << e.what() << "\n";
        rc = 70;
    }
    std::cout.flush();
    std::cerr.flush();
    std::fflush(stdout);
    std::fflush(stderr);
    return rc;
}

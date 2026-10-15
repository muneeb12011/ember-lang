// toolchain.hpp - turning generated assembly into an executable: the embedded C runtime is compiled once
// into a per-user cache (keyed by a hash of its source), then linked with every program.
#pragma once
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <iterator>
#include <vector>

#include "rt_data.hpp"

inline std::string shq(const std::string& s) {
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else r += c;
    }
    return r + "'";
}

// A directory only we can write to. (A shared, predictable path such as /tmp/x.o would let another user plant
// code that every compiled program then links.)
inline bool privateDir(const std::string& d) {
    mkdir(d.c_str(), 0700);
    struct stat st;
    if (lstat(d.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode) && st.st_uid == getuid() && (st.st_mode & 022) == 0;
}

inline std::string cacheDir() {
    std::vector<std::string> cands;
    if (const char* x = getenv("XDG_CACHE_HOME"))
        if (*x) { mkdir(x, 0700); cands.push_back(std::string(x) + "/ember"); }
    if (const char* h = getenv("HOME"))
        if (*h) { mkdir((std::string(h) + "/.cache").c_str(), 0700); cands.push_back(std::string(h) + "/.cache/ember"); }
    cands.push_back("/tmp/ember-cache-" + std::to_string((long)getuid()));
    for (const std::string& c : cands)
        if (privateDir(c)) return c;
    return "";
}

inline uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

// Returns the path of the compiled runtime object, building it if necessary ("" on failure).
inline std::string runtimeObject(std::string& err) {
    std::string dir = cacheDir();
    if (dir.empty()) { err = "no private cache directory available"; return ""; }
    char hex[24];
    snprintf(hex, sizeof hex, "%016llx", (unsigned long long)fnv1a(kRuntimeSource));
    std::string obj = dir + "/rt-" + hex + ".o";
    if (access(obj.c_str(), R_OK) == 0) return obj;
    std::string pid = std::to_string((long)getpid());
    std::string tc = dir + "/rt-" + hex + "." + pid + ".c", to = dir + "/rt-" + hex + "." + pid + ".o";
    {
        std::ofstream f(tc);
        f << kRuntimeSource;
    }
    std::string cmd = "gcc -O2 -fno-strict-aliasing -c " + shq(tc) + " -o " + shq(to);
    int rc = std::system(cmd.c_str());
    std::remove(tc.c_str());
    if (rc != 0) { std::remove(to.c_str()); err = "compiling the runtime failed (is gcc installed?)"; return ""; }
    if (rename(to.c_str(), obj.c_str()) != 0) { std::remove(to.c_str()); err = "cannot install runtime object"; return ""; }
    return obj;
}

// Assemble `asmPath`, link it with the runtime into `out`. Returns 0 on success.
inline int linkProgram(const std::string& asmPath, const std::string& out, std::string& err) {
    std::string rt = runtimeObject(err);
    if (rt.empty()) return 1;
    std::string cmd = "gcc -no-pie " + shq(asmPath) + " " + shq(rt) + " -o " + shq(out) + " -lm -lpthread";
    if (std::system(cmd.c_str()) != 0) { err = "assembling/linking failed"; return 1; }
    return 0;
}

// ---- WebAssembly: the runtime module is built from its embedded C source by clang (cached like the native object)
inline std::string wasmRuntime(std::string& err) {
    std::string dir = cacheDir();
    if (dir.empty()) { err = "no private cache directory available"; return ""; }
    char hex[24];
    snprintf(hex, sizeof hex, "%016llx", (unsigned long long)fnv1a(kWasmRuntimeSource));
    std::string mod = dir + "/rt-" + hex + ".wasm";
    if (access(mod.c_str(), R_OK) == 0) return mod;
    std::string pid = std::to_string((long)getpid());
    std::string tc = dir + "/rt-" + hex + "." + pid + ".c", to = dir + "/rt-" + hex + "." + pid + ".wasm";
    {
        std::ofstream f(tc);
        f << kWasmRuntimeSource;
    }
    std::string cmd = "clang --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin -Wl,--no-entry -Wl,--allow-undefined "
                      "-Wl,--import-memory -Wl,--global-base=16777216 -Wl,-z,stack-size=1048576 " + shq(tc) + " -o " + shq(to);
    int rc = std::system(cmd.c_str());
    std::remove(tc.c_str());
    if (rc != 0) { std::remove(to.c_str()); err = "building the wasm runtime failed (the wasm target needs clang and wasm-ld)"; return ""; }
    if (rename(to.c_str(), mod.c_str()) != 0) { std::remove(to.c_str()); err = "cannot install the wasm runtime"; return ""; }
    return mod;
}

inline std::string base64(const std::vector<uint8_t>& b) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string r;
    size_t i = 0;
    for (; i + 2 < b.size(); i += 3) {
        uint32_t v = (uint32_t)b[i] << 16 | (uint32_t)b[i + 1] << 8 | b[i + 2];
        r += t[v >> 18]; r += t[(v >> 12) & 63]; r += t[(v >> 6) & 63]; r += t[v & 63];
    }
    if (i + 1 == b.size()) {
        uint32_t v = (uint32_t)b[i] << 16;
        r += t[v >> 18]; r += t[(v >> 12) & 63]; r += "==";
    } else if (i + 2 == b.size()) {
        uint32_t v = (uint32_t)b[i] << 16 | (uint32_t)b[i + 1] << 8;
        r += t[v >> 18]; r += t[(v >> 12) & 63]; r += t[(v >> 6) & 63]; r += '=';
    }
    return r;
}

// write `out` as one executable JS file holding both modules and the loader (run it with node)
inline int writeWasmProgram(const std::vector<uint8_t>& prog, const std::string& out, std::string& err) {
    std::string rtPath = wasmRuntime(err);
    if (rtPath.empty()) return 1;
    std::ifstream rf(rtPath, std::ios::binary);
    std::vector<uint8_t> rt((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
    std::ofstream o(out, std::ios::binary);
    if (!o) { err = "cannot write " + out; return 1; }
    o << "#!/usr/bin/env node\n// built by embc --target wasm\nconst EMBER_RT = \"" << base64(rt) << "\", EMBER_PROG = \""
      << base64(prog) << "\";\n" << kWasmLoader;
    o.close();
    chmod(out.c_str(), 0755);
    return 0;
}

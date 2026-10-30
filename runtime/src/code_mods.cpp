// Code mods (nx/mod.h) and the game's own exefs patches.
//
// Each enabled folder in <data root>/mods/ (the RomFS mods' folders: a name starting with '-' is off) may hold:
//   - shared libraries (*.dll on Windows): loaded, and their nx_mod_init called with the API;
//   - IPS patches for main, the way Switch mods ship them (exefs/<build id>.ips, or
//     atmosphere/exefs_patches/<any>/<build id>.ips): applied to main's data. Their bytes in main's code are reported
//     and skipped, since the code is recompiled ahead of time (a patch to it needs a recompile with it applied).
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nx/game.h>
#include <nx/mod.h>
#include <nx/module.h>
#include <nx/runtime.h>

#include "platform.h"

extern const nx::ModuleInfo nx_module_main;

namespace nx {

namespace {

namespace fs = std::filesystem;

std::string utf8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

Fn api_hook(uint64_t offset, Fn mine) {
    const LoadedModule* m = module_at(nx_module_main.base);
    if (offset >= nx_module_main.text_size || offset % 4) return nullptr;
    Fn game = m->table[offset / 4];
    if (!game) return nullptr;
    m->table[offset / 4] = mine;
    return game;
}

void api_log(const char* fmt, ...) {
    char b[1024];
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    log("mod: %s", b);
}

// main's build id (the first 8 bytes, as hex: what IPS patch names start with), from the NSO header.
std::string main_build_id() {
    std::ifstream f(fs::u8path(plat::data_root() + "/exefs/main"), std::ios::binary);
    unsigned char id[8] = {};
    f.seekg(0x40);
    f.read(reinterpret_cast<char*>(id), sizeof id);
    char hex[17];
    for (int k = 0; k < 8; k++) std::snprintf(hex + 2 * k, 3, "%02X", id[k]);
    return f ? hex : "";
}

// IPS / IPS32: records of (offset, size, bytes) or RLE (offset, 0, count, byte). The offsets count from the NSO's
// start, whose 0x100-byte header is not loaded.
void apply_ips(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    bool ips32 = d.size() >= 5 && std::memcmp(d.data(), "IPS32", 5) == 0;
    if (!ips32 && !(d.size() >= 5 && std::memcmp(d.data(), "PATCH", 5) == 0)) {
        log("mods: %s: not an IPS patch", utf8(path).c_str());
        return;
    }
    size_t at = 5, w = ips32 ? 4 : 3, applied = 0, code = 0;
    auto be = [&](size_t n) {
        uint64_t v = 0;
        for (size_t k = 0; k < n; k++) v = v << 8 | d[at++];
        return v;
    };
    while (at + w <= d.size()) {
        if (std::memcmp(&d[at], ips32 ? "EEOF" : "EOF", w) == 0) break;
        uint64_t off = be(w), size = be(2);
        std::vector<uint8_t> bytes;
        if (size) {
            if (at + size > d.size()) break;
            bytes.assign(d.begin() + at, d.begin() + at + size);
            at += size;
        } else {
            uint64_t n = be(2);
            bytes.assign(n, d[at++]);
        }
        if (off < 0x100) continue;
        uint64_t mem = off - 0x100;
        if (mem < nx_module_main.text + nx_module_main.text_size) {
            code += bytes.size();
            continue;
        }
        for (size_t k = 0; k < bytes.size(); k++) st<uint8_t>(nx_module_main.base + mem + k, bytes[k]);
        applied += bytes.size();
    }
    log("mods: %s: %zu bytes patched in main's data%s", utf8(path).c_str(), applied,
        code ? ", its patches to main's code skipped (the code is recompiled: they need a recompile)" : "");
}

}  // namespace

void load_code_mods() {
    std::error_code ec;
    std::vector<fs::path> mods;
    for (auto& e : fs::directory_iterator(fs::u8path(plat::data_root() + "/mods"), ec))
        if (e.is_directory() && utf8(e.path().filename()).rfind('-', 0) != 0) mods.push_back(e.path());
    std::sort(mods.begin(), mods.end());
    const std::string id = main_build_id();
    static const ModApi api = {kModApiVersion, nx_module_main.base, game::kInfo.id, nullptr, api_hook, call,
                               find_symbol, api_log};
    for (const fs::path& m : mods) {
        for (auto it = fs::recursive_directory_iterator(m, ec); it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (it.depth() >= 4) it.disable_recursion_pending();
            if (!it->is_regular_file()) continue;
            const fs::path& p = it->path();
            std::string ext = utf8(p.extension()), name = utf8(p.stem());
            for (char& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            for (char& ch : name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            if (ext == ".ips" && !id.empty() && name.rfind(id, 0) == 0) {
                apply_ips(p);
            } else if (ext == plat::library_extension()) {
                void* lib = plat::load_library(utf8(p));
                auto init = lib ? reinterpret_cast<void (*)(const ModApi*)>(plat::library_symbol(lib, "nx_mod_init"))
                                : nullptr;
                if (!init) {
                    log("mods: %s: %s", utf8(p).c_str(), lib ? "no nx_mod_init, skipped" : "could not be loaded");
                    continue;
                }
                ModApi mine = api;
                mine.mod_dir = (new std::string(utf8(m)))->c_str();  // kept for the process's life
                log("mods: %s: code mod loaded", utf8(p).c_str());
                init(&mine);
            }
        }
    }
}

}  // namespace nx

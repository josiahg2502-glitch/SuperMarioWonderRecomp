// Code mods: a shared library in <data root>/mods/<name>/ (Windows: *.dll) that exports
//
//     extern "C" NX_MOD_EXPORT void nx_mod_init(const nx::ModApi* api);
//
// The runtime loads every such library at start, after the game's constructors and before its main, and calls
// nx_mod_init. A mod is built against these headers (runtime/include) with the same compiler as the recomp (clang-cl
// on Windows); docs/MODS.md has a worked example (examples/mods/hello).
//
// What a mod can do through the API:
//   - replace a game function: hook(offset in main, replacement) returns the game's own function, which the
//     replacement may call. Every function the game reaches through a function pointer (virtual functions,
//     callbacks, the enum and UI tables) can be replaced; a direct call inside the recompiled code goes straight to
//     its callee and is not affected.
//   - call any game function (call), read and write game memory (guest addresses are host addresses: nx::ld/st),
//     find a symbol of the SDK (find_symbol), log.
#pragma once

#include <cstdint>

#include "cpu.h"

#if defined(_WIN32)
#define NX_MOD_EXPORT __declspec(dllexport)
#else
#define NX_MOD_EXPORT __attribute__((visibility("default")))
#endif

namespace nx {

constexpr uint32_t kModApiVersion = 1;

struct ModApi {
    uint32_t version;     // kModApiVersion of the runtime; a mod built for a newer one should not go on
    uint64_t main_base;   // where main (the game's code) is loaded: a main offset + main_base is its address
    const char* game_id;  // "wonder"
    const char* mod_dir;  // this mod's folder (UTF-8)
    Fn (*hook)(uint64_t main_offset, Fn replacement);  // the replaced function, or nullptr when there is none
    void (*call)(CPU& c, uint64_t address);             // calls a game function with c's registers
    uint64_t (*find_symbol)(const char* name);           // an SDK / imported symbol's address, 0 when unknown
    void (*log)(const char* fmt, ...);
};

}  // namespace nx

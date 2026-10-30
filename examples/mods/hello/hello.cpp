// A code mod (nx/mod.h, docs/MODS.md): logs every choice made in the pause menu, then lets the game act on it.
// Built by CMake as mods/hello/hello.dll next to the recomp; copy that folder into <data root>/mods/ to use it.
#include <nx/mod.h>

namespace {

const nx::ModApi* g_api;
nx::Fn g_game;  // the function this mod replaced

// game::sequence::PauseResult's from-string callback (main+0x1d3415c in 1.2.0): bool (int* out, const char** name).
void pause_choice(nx::CPU& c) {
    g_api->log("hello: pause menu choice %s", *reinterpret_cast<const char* const*>(c.x[1]));
    g_game(c);
}

}  // namespace

extern "C" NX_MOD_EXPORT void nx_mod_init(const nx::ModApi* api) {
    if (api->version < nx::kModApiVersion) return;
    g_api = api;
    g_game = api->hook(0x1d3415c, pause_choice);
    api->log("hello from %s", api->mod_dir);
}

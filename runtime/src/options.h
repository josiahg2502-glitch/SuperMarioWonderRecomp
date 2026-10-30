// The player's recomp options (the game's Settings screen shows them: games/wonder/menu.cpp). Read from
// plat::setting at start; the menu changes them live and writes them back to <data root>/user/settings.txt.
#pragma once

#include <atomic>
#include <cstdint>

namespace nx::opt {

enum Option : int {
    kRenderScale,  // 0: 1x, 1: 1.5x, 2: 2x, 3: 3x, 4: 4x (applies at the next start)
    kFullscreen,   // 0: windowed, 1: fullscreen
    kFastForward,  // 0: off, 1: hold Tab / ZL+ZR+R-stick, 2: the same keys toggle it
    kFastSpeed,    // 0: 2x, 1: 3x, 2: 4x, 3: as fast as possible
    kFastLoads,    // faster menus, saves and loads: 0 off, 1 on
    kSaveAnywhere, // 0 off, 1 on: the pause menu's Save button saves in a course too
    kQuitButton,   // 0 off, 1 on: the pause menu's Quit Game button quits
    kAutoUpdate,   // 0 off, 1 on: updater.cpp checks for and installs new releases
    kCount
};

extern std::atomic<int> g_value[kCount];
inline int get(Option o) { return g_value[o].load(std::memory_order_relaxed); }
int choices(Option o);
const char* key(Option o);  // its settings.txt key
void set(Option o, int v);  // takes effect now and is saved
void load();                // once, at start

float render_scale_of(int v);
// The vsync thread's tick when the game should run faster than 60 Hz (fast-forward held or toggled, or a load,
// save or menu in progress with kFastLoads on); 0 when it runs at 60 Hz.
// The vsync thread calls it once a tick; fast() is its last answer, for other threads.
int64_t fast_tick_ns();
bool fast();
// Something worth speeding through has happened (a menu button chosen, a file read or written).
void busy(int64_t for_ns);

}  // namespace nx::opt

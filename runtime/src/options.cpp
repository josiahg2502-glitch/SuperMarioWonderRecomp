// The player's recomp options (options.h).
#include "options.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <nx/runtime.h>

#include "platform.h"

namespace nx::opt {

std::atomic<int> g_value[kCount];
std::atomic<bool> g_quit_after_save{false};

namespace {

struct Def {
    const char* key;
    int choices, initial;
};
constexpr Def kDefs[kCount] = {
    {"render_scale", 5, 0}, {"fullscreen", 2, 0},     {"fast_forward", 3, 1}, {"fast_speed", 4, 1},
    {"fast_loads", 2, 1},   {"save_anywhere", 2, 1}, {"quit_button", 2, 1},  {"auto_update", 2, 1},
};
constexpr float kScales[] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f};

std::mutex g_file_m;
std::atomic<int64_t> g_busy_until{0};
std::atomic<bool> g_fast{false};

std::string settings_path() { return plat::data_root() + "/user/settings.txt"; }

// settings.txt keeps the stored value: render_scale as the scale itself (NX_RENDER_SCALE reads it), the rest as
// the choice's number.
std::string stored(Option o, int v) {
    if (o != kRenderScale) return std::to_string(v);
    char b[16];
    std::snprintf(b, sizeof b, "%g", kScales[v]);
    return b;
}

int parsed(Option o, const std::string& s) {
    if (o == kRenderScale) {
        float f = float(std::atof(s.c_str()));
        int best = 0;
        for (int k = 0; k < 5; k++)
            if (std::abs(kScales[k] - f) < std::abs(kScales[best] - f)) best = k;
        return best;
    }
    return std::clamp(std::atoi(s.c_str()), 0, kDefs[o].choices - 1);
}

void save(Option o, int v) {
    std::lock_guard lk(g_file_m);
    std::vector<std::string> lines;
    {
        std::ifstream f(std::filesystem::u8path(settings_path()));
        for (std::string line; std::getline(f, line);) {
            while (!line.empty() && line.back() == '\r') line.pop_back();
            std::string k = kDefs[o].key;
            if (!(line.size() > k.size() && line.compare(0, k.size(), k) == 0 && line[k.size()] == '='))
                lines.push_back(line);
        }
    }
    lines.push_back(std::string(kDefs[o].key) + "=" + stored(o, v));
    plat::make_dirs(plat::data_root() + "/user");
    std::ofstream f(std::filesystem::u8path(settings_path()), std::ios::trunc);
    for (const auto& l : lines) f << l << "\n";
}

}  // namespace

int choices(Option o) { return kDefs[o].choices; }
const char* key(Option o) { return kDefs[o].key; }
float render_scale_of(int v) { return kScales[std::clamp(v, 0, 4)]; }

void load() {
    for (int o = 0; o < kCount; o++) {
        std::string s = plat::setting(kDefs[o].key);
        g_value[o] = s.empty() ? kDefs[o].initial : parsed(Option(o), s);
    }
    if (get(kFullscreen)) plat::set_fullscreen(true);
}

void set(Option o, int v) {
    v = std::clamp(v, 0, kDefs[o].choices - 1);
    if (g_value[o].exchange(v) == v) return;
    log("options: %s = %s", kDefs[o].key, stored(o, v).c_str());
    if (o == kFullscreen) plat::set_fullscreen(v != 0);
    save(o, v);
}

namespace {
int64_t tick();
}

void busy(int64_t for_ns) {
    int64_t until = int64_t(plat::monotonic_ns()) + for_ns;
    int64_t old = g_busy_until.load();
    while (old < until && !g_busy_until.compare_exchange_weak(old, until)) {
    }
}

bool fast() { return g_fast.load(std::memory_order_relaxed); }

int64_t fast_tick_ns() {
    int64_t t = tick();
    g_fast = t != 0;
    return t;
}

namespace {
int64_t tick() {
    static constexpr int64_t kTick[] = {8'333'333, 5'555'556, 4'166'667, 2'000'000};  // 2x, 3x, 4x, 500 Hz
    // Toggle mode: each press of the keys flips it.
    static bool was_down = false, toggled = false;
    int mode = get(kFastForward);
    bool down = mode && plat::fast_forward();
    if (mode == 2 && down && !was_down) toggled = !toggled;
    was_down = down;
    if (mode != 2) toggled = false;
    if ((mode == 1 && down) || toggled) return kTick[get(kFastSpeed)];
    if (get(kFastLoads) && int64_t(plat::monotonic_ns()) < g_busy_until.load()) return kTick[2];
    return 0;
}
}  // namespace

}  // namespace nx::opt

// The recomp's entries in Wonder's own menus (docs/MENU.md). tools/wonder_menu_mod.py adds the buttons and texts to
// the game's data; this file gives them meaning.
//
// Pause menu "Quit Game": its button (data) reports game::sequence::PauseResult "QuitToDesktop", a name the game does
// not know. Enum types are registered with callbacks the game only reaches through function pointers, so swapping a
// function-table entry replaces one for the whole game. The pause screen turns the chosen button's name into its value
// with the type's from-string callback when the button is chosen (not before): that call is the moment to quit.
//
// The world map's menu has the same two buttons, reporting the same names through game::sequence::MainMenuResult.
//
// Pause menu "Recomp Settings": reports "RecompOption", which opens the game's own Settings screen (as "Option" does)
// with the recomp's options (options.h) in its 8 rows instead of the game's. The screen builds each row's texts from
// a table of option-type names ("<name>_Subject", "<name>_Selection00"... in GameMsg/Option.msbt, where the tool adds
// the recomp's) and a table of choice counts, and keeps the row values in its own int[8]; its open, left, right and
// confirm handlers are only reached through function pointers. While the recomp's page is open the tables hold the
// recomp's names and counts, the values are the recomp's options, and confirming hands the game back its own values.
#include <cstring>

#include <nx/game.h>
#include <nx/module.h>
#include <nx/runtime.h>

#include "options.h"
#include "platform.h"

extern const nx::ModuleInfo nx_module_main;

namespace nx::wonder {

namespace {

// main (1.2.0) offsets.
constexpr uint64_t kPauseResultFromString = 0x1d3415c;     // bool (int* out, const char** name)
constexpr uint64_t kMainMenuResultFromString = 0x1d342c8;  // the world map's menu: the same
constexpr uint64_t kOptionOpen = 0x1d88ecc;             // (screen): reads the options into the row values
constexpr uint64_t kOptionLeft = 0x1d8a920;             // (screen): the cursor's row, previous choice
constexpr uint64_t kOptionRight = 0x1d8aad4;            // (screen): next choice
constexpr uint64_t kOptionDecide = 0x1d89394;           // (screen, event): OK: writes the row values to the options
constexpr uint64_t kOptionRow = 0x1d89700;              // (screen, element, ...): draws one row
constexpr uint64_t kOptionNames = 0x366cdb0;            // const char*[8], relocated data
constexpr uint64_t kOptionCounts = 0x2aa88b0;           // uint32_t[8]
// In the Settings screen object.
constexpr uint64_t kValues = 0x678, kValuesAtOpen = 0x698;  // int[8] each
// Bytes deciding which rows the game greys out: +0x710 set greys row 6 (the Talking Flower's voice off), +0x712 and
// +0x713 clear grey rows 3 (rumble off in the system settings) and 4 (no badges yet).
constexpr uint64_t kVoiceOff = 0x710, kRumbleOn = 0x712, kHaveBadges = 0x713;

constexpr int kRows = 8;
static_assert(opt::kCount == kRows, "one recomp option per Settings row");
const char* const kRecompNames[kRows] = {"RecompRenderScale", "RecompFullscreen", "RecompFastForward",
                                         "RecompFastSpeed",   "RecompFastLoads",  "RecompSaveAnywhere",
                                         "RecompQuitButton",  "RecompAutoUpdate"};

Fn g_pause_result_from_string, g_main_menu_result_from_string, g_open, g_left, g_right, g_decide, g_row;
uint64_t g_base;
bool g_recomp_next = false;    // the next Settings screen is the recomp's page
uint64_t g_recomp_screen = 0;  // the Settings screen showing it
uint64_t g_game_names[kRows];
uint32_t g_game_counts[kRows];
int32_t g_game_values[kRows];

void use_tables(bool recomp) {
    for (int k = 0; k < kRows; k++) {
        st<uint64_t>(g_base + kOptionNames + 8 * k, recomp ? uint64_t(kRecompNames[k]) : g_game_names[k]);
        st<uint32_t>(g_base + kOptionCounts + 4 * k,
                     recomp ? uint32_t(opt::choices(opt::Option(k))) : g_game_counts[k]);
    }
}

// A menu (the pause menu in a course, the world map's) and the names of its own choices the recomp's turn into.
struct Menu {
    const char* what;
    Fn* game;           // the enum's from-string callback
    const char* close;  // closes the menu
    const char* leave;  // the game saves: Exit Course in a course, Save on the world map
};
const Menu kPause = {"pause menu", &g_pause_result_from_string, "Continue", "Out"};
const Menu kMainMenu = {"world map menu", &g_main_menu_result_from_string, "Close", "Save"};

// x1 is the menu screen's copy of the chosen button's name (a stack buffer: char*, then its capacity). After this
// callback the screen stores the hash of that text in GameData (PauseResult, MainMenuResult), and the menu's sequence
// picks its branch by it: so the recomp's names are rewritten in the buffer to the game's own.
void menu_result_from_string(CPU& c, const Menu& m) {
    char* name = reinterpret_cast<char*>(ld<uint64_t>(c.x[1]));
    uint32_t capacity = ld<uint32_t>(c.x[1] + 8);
    if (opt::get(opt::kFastLoads)) opt::busy(600'000'000);  // the menu's out animation and what follows
    const char* as = nullptr;
    if (std::strcmp(name, "QuitToDesktop") == 0) {
        if (!opt::get(opt::kQuitButton)) {
            as = m.close;  // the option is off: the button closes the menu
        } else if (opt::get(opt::kSaveAnywhere)) {
            // Save Before Quitting: Wonder keeps a course's progress when the course is left, so Quit Game leaves it
            // the way Exit Course does (the game asks, then saves), or saves on the world map, and the process ends
            // once the save is committed (hle_fs.cpp).
            log("menu: Quit Game chosen in the %s: saving first", m.what);
            opt::g_quit_after_save = true;
            as = m.leave;
        } else {
            log("menu: Quit Game chosen in the %s", m.what);
            plat::exit_process(0);  // what closing the window does
        }
    } else if (std::strcmp(name, "RecompOption") == 0) {
        log("menu: Recomp Settings chosen in the %s", m.what);
        g_recomp_next = true;
        as = "Option";
    }
    if (as && std::strlen(as) < capacity) std::strcpy(name, as);
    (*m.game)(c);
}
void pause_result_from_string(CPU& c) { menu_result_from_string(c, kPause); }
void main_menu_result_from_string(CPU& c) { menu_result_from_string(c, kMainMenu); }

void available(uint64_t screen) {  // every recomp row can be changed
    st<uint8_t>(screen + kVoiceOff, 0);
    st<uint8_t>(screen + kRumbleOn, 1);
    st<uint8_t>(screen + kHaveBadges, 1);
}

void option_open(CPU& c) {
    uint64_t screen = c.x[0];
    bool recomp = g_recomp_next;
    g_recomp_next = false;
    g_recomp_screen = recomp ? screen : 0;
    use_tables(recomp);
    g_open(c);
    if (!recomp) return;
    log("menu: Recomp Settings opened");
    for (int k = 0; k < kRows; k++) {
        g_game_values[k] = ld<int32_t>(screen + kValues + 4 * k);
        int32_t v = opt::get(opt::Option(k));
        st<int32_t>(screen + kValues + 4 * k, v);
        st<int32_t>(screen + kValuesAtOpen + 4 * k, v);
    }
    available(screen);
}

void option_row(CPU& c) {
    if (c.x[0] == g_recomp_screen) available(c.x[0]);
    g_row(c);
}

// Left / right: the game steps the row's value; the recomp applies it at once and keeps it.
void option_step(CPU& c, Fn game) {
    uint64_t screen = c.x[0];
    game(c);
    if (screen != g_recomp_screen) return;
    for (int k = 0; k < kRows; k++) {
        int32_t v = ld<int32_t>(screen + kValues + 4 * k);
        opt::set(opt::Option(k), v);
        st<int32_t>(screen + kValuesAtOpen + 4 * k, v);  // nothing for the game to ask about on the way out
    }
}
void option_left(CPU& c) { option_step(c, g_left); }
void option_right(CPU& c) { option_step(c, g_right); }

void option_decide(CPU& c) {
    uint64_t screen = c.x[0];
    if (screen == g_recomp_screen) {  // the game writes its own options back as they were
        for (int k = 0; k < kRows; k++) st<int32_t>(screen + kValues + 4 * k, g_game_values[k]);
        log("menu: Recomp Settings closed");
    }
    g_decide(c);
}

Fn swap(uint64_t offset, Fn mine) {
    Fn* table = module_at(nx_module_main.base)->table;
    Fn game = table[offset / 4];
    if (!game) fatal("menu: no function at main+%llx (not the 1.2.0 code?)", (unsigned long long)offset);
    table[offset / 4] = mine;
    return game;
}

}  // namespace

void menu_init() {
    g_base = nx_module_main.base;
    for (int k = 0; k < kRows; k++) {
        g_game_names[k] = ld<uint64_t>(g_base + kOptionNames + 8 * k);
        g_game_counts[k] = ld<uint32_t>(g_base + kOptionCounts + 4 * k);
    }
    g_pause_result_from_string = swap(kPauseResultFromString, pause_result_from_string);
    g_main_menu_result_from_string = swap(kMainMenuResultFromString, main_menu_result_from_string);
    g_open = swap(kOptionOpen, option_open);
    g_left = swap(kOptionLeft, option_left);
    g_right = swap(kOptionRight, option_right);
    g_decide = swap(kOptionDecide, option_decide);
    g_row = swap(kOptionRow, option_row);
}

}  // namespace nx::wonder

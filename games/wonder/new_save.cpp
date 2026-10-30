// A new player's first start: Wonder 1.2.0 crashes at boot when its save folder has no game_data.sav (a lookup in its
// user table, main+0x76c3fc, finds no entry on that path), so before the game starts the runtime writes the game's
// own empty first save there (new_save.inc). The title screen then shows "New Game" as usual.
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nx/runtime.h>

#include "platform.h"

namespace nx::wonder {

namespace {
#include "new_save.inc"
}

void seed_new_save() {
    namespace fs = std::filesystem;
    fs::path dir = fs::u8path(plat::data_root()) / "user" / "save", file = dir / "game_data.sav";
    std::error_code ec;
    if (fs::exists(file, ec)) return;
    std::vector<char> data(kNewSaveSize, 0);
    for (const SaveRun& r : kNewSave) std::copy(r.bytes, r.bytes + r.size, data.begin() + r.offset);
    fs::create_directories(dir, ec);
    std::ofstream(file, std::ios::binary).write(data.data(), std::streamsize(data.size()));
    log("save: none yet, wrote a new player's game_data.sav");
}

}  // namespace nx::wonder

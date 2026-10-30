# The recomp's options in Wonder's own menus

The quality bar puts every recomp option in the game's own pause and main menus, styled like the game: no launcher,
no overlay. This is how Wonder's menus are built, how the recomp adds to them, and the plan for each option.

## How Wonder builds its menus

Nearly all of it is data:

- **Layouts** are NintendoWare `bflyt` in `Layout/<Screen>.Nin_NX_NVN.blarc.zs` (a zstd SARC). A button is a part
  pane (`prt1`) using a shared button layout (`LCmnBtn`, `LCmnBtnIcon`...), e.g. the pause screen's `L_BtnContinue`,
  `L_RetryBtn`, `L_BtnExit` and the icon row `L_PlayerChangeBtn`, `L_OptionBtn`, `L_HelpBtn`, `L_AmiiboBtn`. The
  screen's In/Out animations move the containers (`N_Left`, `N_Right`), so a new pane under the same container comes
  and goes with the others.
- **Which panes are buttons, and what they do** is `Gyml/UIExtraScreenParam/<Screen>.game__UIExtraScreenParam.bgyml`
  in `Pack/Bootup.Nin_NX_NVN.pack.zs` (BYML v7). Each `ButtonInfoGenerateTable` entry names a pane and a
  `DecideInfoGenerateTable`: an enum type, a value, and the GameData variable the value is written to when the button
  is chosen. Pause: `game::sequence::PauseResult` into `PauseResult`. World-map menu: `game::sequence::MainMenuResult`
  into `MainMenuResult`.
- **What happens next** is an AINB graph (the pause one is
  `Sequence/_LocalModule_d9f012ee2b26.module.ainb` in `Pack/PostResident.pack.zs`): node `GetDecideButtonResult`
  (screen `Pause`, enum `game::sequence::PauseResult`) then one branch per value (`IsSelectExit` -> `AskIfExitCourse`
  window, `Option` -> `UISeq_Option`...).
- **Text** comes from `Mals/<lang>.Product.120.sarc.zs`, `LayoutMsgRef/<Layout>Ref.msbt`, label
  `<part pane>-<text pane>` (e.g. `L_BtnExit-T_BtnName` = "Exit Course").
- **The Settings screen** (pause/menu "Settings", layout `Option`, rows `LOptionParts` in a scrolling list) lists the
  option types `DashCtrlType`, `SquatCtrlType`, `SwingCtrlType`, `HDVibrationLevelType`, `BadgeSelectOpenType`,
  `TalkFlowerType`, `TalkFlowerLanguageType`, `TelopDisplayType`; their text is `GameMsg/Option.msbt`
  `<Type>_Subject` / `<Type>_SelectionNN`. The count (8) is a constant in the code (e.g. `main+0x1d89a10`
  `cmp w1, #7`), and the names are a pointer table at `main+0x366cdb8`.

The code side: `main` has no symbols for game code, but **enum types** are registered with three callbacks (from
string, to string, for each value) stored as function pointers (the registry is filled by `main+0x585d74`). The
recompiled code reaches them only through `nx::call`, which looks the target up in the module's function table, so
replacing a table entry (`module_at(base)->table[offset / 4]`) replaces the function for the whole game without
regenerating code. The same works for any virtual function or other function-pointer target; direct `bl` calls are
compiled to direct C++ calls and cannot be swapped this way.

## What is built: "Quit Game" and "Recomp Settings" in the pause menu and the world map's menu

- `tools/wonder_menu_mod.py` writes the RomFS mod `private/wonder/mods/recomp-menu/` from the extracted RomFS:
  - `L_QuitBtn` and `L_RecompBtn`: copies of the `L_BtnExit` part. The left column is re-spaced in steps of 120,
    and the icon row moves down to -445, so the icons' tooltips clear the new buttons.
  - Their button entries: `PauseResult` = `QuitToDesktop` and `RecompOption`.
  - Their labels in every language.
  - The recomp's option texts in `GameMsg/Option.msbt` (English in every language for now).
  - The size table.

  Run it once after extracting the RomFS.
- `games/wonder/menu.cpp` swaps `PauseResult`'s from-string callback (`main+0x1d3415c`). The pause screen calls it
  when a button is **chosen** (not when the screen opens or the cursor moves: checked in game). It passes the
  button's name copied into a stack buffer (`char*`, then its capacity). After the callback the screen stores the
  **hash of that text** in GameData `PauseResult` (`main+0x6093f0`), and the pause sequence picks its branch by that
  hash. A name the game doesn't know, like `RecompOption`, therefore matches no branch and just closes the menu. So
  the callback rewrites the text in the buffer to one of the game's own names:
  - `QuitToDesktop` exits the way closing the window does. With "Save Before Quitting" on, it becomes `Out` instead:
    the game asks "Exit this course?", leaves the course and saves, and `hle_fs.cpp` ends the process at the save's
    commit. With the Quit Game option off, it becomes `Continue`.
  - `RecompOption` becomes `Option` (the game's Settings screen) and marks the next Settings screen as the recomp's
    page.

### The recomp's page on the game's Settings screen

The Settings screen (`Option`) has 8 rows (`SlideList` `ElementNumMax` 8). Its code is data-driven in ways the
runtime can use:

- **Each row's texts** are `<name>_Subject` and `<name>_SelectionNN` in `GameMsg/Option.msbt`. The name comes from a
  table of 8 type names (`main+0x366cdb0`, relocated pointers). The row's number of choices comes from a table of 8
  counts (`main+0x2aa88b0`).
- **The row values** are an `int[8]` in the screen object at +0x678, with a copy at +0x698 taken at open.
- **Handlers**, all reached only through function pointers, so `menu.cpp` swaps them in the function table:
  - open: `main+0x1d88ecc`. It reads the game's options into +0x678.
  - left and right: `main+0x1d8a920` and `main+0x1d8aad4`. They step the cursor's row value modulo its count.
  - OK: `main+0x1d89394`. It writes the values back to the game's options.
  - draw a row: `main+0x1d89700`.
- **Availability bytes** at +0x710..0x713 grey rows out:
  - +0x710 set: the Talking Flower's voice is off, so row 6 greys out.
  - +0x712 clear: rumble is off in the system settings, so row 3 greys out.
  - +0x713 clear: there are no badges yet, so row 4 greys out.

  The "can this row change" check is `main+0x1d8b864`.

On the recomp's page the runtime does the following:

- **Before open:** it puts the recomp's names (`RecompRenderScale`...) and counts into the two tables. Every other
  open puts the game's own back.
- **After open:** it saves the game's values and writes the recomp's options (`runtime/src/options.h`, in row order)
  into both copies.
- **Before each row is drawn:** it marks every row available.
- **On left or right:** it applies the new value at once (`opt::set`, saved to `user/settings.txt`) and refreshes
  the at-open copy, so leaving with B asks nothing.
- **On OK:** it hands the game back its own values first, so the game's options are written back unchanged.

Rows 1 and 5 have special cases in the game's code. Row 1 can ask for `Selection02`, so the tool gives Fullscreen a
third text. A row-5 value of 2 greys out row 6, and the recomp's row 5 has two choices, so that never happens.

Two things the data side has to respect, both found the hard way (the game crashes at boot otherwise):

- **The resource size table** (`System/Resource/ResourceSizeTable.Product.120.rsizetable.zs`, RESTBL: CRC32 of the
  path -> size) sizes the buffer each file is loaded into: every archive that grows needs its entry grown as much.
- **A pack member's own entry** sizes what it is parsed into, not just its bytes. The pause screen's bgyml is 4660
  bytes but its entry is 19136, and one more button overflows it (a failed allocation of 0x60 bytes at
  `main+0x939b6c`). The tool grows it by 16 KiB.

Tested headless (scripted input, a save on the world map, then course 1-1):

- Both buttons show with the game's look and animation, and the cursor reaches them.
- Recomp Settings opens the game's Settings screen with the recomp's rows, all 8 available. Left and right change
  and save them, and B returns to the pause menu.
- Afterwards the game's own Settings screen shows its own rows and values, unchanged.
- Quit Game with Save Before Quitting asks, saves, then exits with code 0.

## The world map's menu

`MainMenu` gets the same two buttons, as copies of `L_TitleBtn`. Save and Return to Title move left (x -240) and the
recomp's two sit to their right: Quit Game beside Save, Recomp Settings beside Return to Title. The buttons report
the same names through `game::sequence::MainMenuResult`, whose from-string callback is `main+0x1d342c8` (registered
next to `PauseResult`'s by `main+0x585d74`).

- `RecompOption` becomes `Option`.
- `QuitToDesktop` with Save Before Quitting becomes `Save`: the game asks "Save progress?", saves, and the process
  ends at the commit.

Its bgyml entry gets 64 KiB of room.

The cursor's links in this menu don't follow position alone (Characters DOWN lands on the right-hand top button).
With Recomp Settings in the top-right spot, choosing it hung the game: the main thread waited forever on a heap
lock at `main+0x937360`. In the bottom-right spot it works, so the layout keeps it there.

## Not done

- **Translations** of the option texts.

Choices are kept in `<data root>/user/settings.txt` as `key=value` lines (`render_scale=1.5`). On PC `plat::setting`
reads that file after the `NX_<KEY>` environment variables. Android's `config.txt` uses the same format.

## Mods and updates

The mod replaces whole files: another mod that replaces `Layout/Pause...`, `Pack/Bootup...` or a `Mals` archive and
sorts before `recomp-menu` hides the button (and if it replaces only the layout or only the pack, the button table and
the layout disagree). `--romfs` takes another RomFS folder with all of the files, e.g. one with such a mod merged in.
The offset in `menu.cpp` is for 1.2.0 (a version with no function starting there stops at start with a message).

# Mods

Mods live in `<data root>/mods/<name>/`. That is `private/wonder/mods/` in a development tree and `data\mods\` in
an installed release. Folders load in name order, and a folder whose name starts with `-` is off. A mod folder can
hold any mix of:

| What | Where in the folder | Effect |
|---|---|---|
| RomFS files | `romfs/`, `contents/010015100B514000/romfs/` or `atmosphere/contents/010015100B514000/romfs/` | Replace or add game files (textures, models, music, text, gameplay data) |
| Code mods | any `*.dll` (Windows) | Loaded at start; see below |
| ExeFS patches | `exefs/<build id>.ips`, `atmosphere/exefs_patches/<name>/<build id>.ips` | Data bytes applied; code bytes skipped |

`nx.log` lists every mod, every file taken from one, every code mod loaded and every patch applied.

## Code mods

A code mod is a shared library that exports one function:

```cpp
#include <nx/mod.h>

extern "C" NX_MOD_EXPORT void nx_mod_init(const nx::ModApi* api);
```

The runtime calls it once, after the game's static constructors and before the game's `nnMain`. The API
(`runtime/include/nx/mod.h`) gives:

- `hook(main offset, replacement)`: replaces a game function and returns the game's own, so the replacement can call
  it. The offset is the function's address in `main` (1.2.0).
- `call(cpu, address)`: calls any game function with the registers in `cpu`.
- `find_symbol(name)`: the address of an SDK or imported symbol.
- `log(fmt, ...)`: a line in `nx.log`.
- `main_base`, `game_id`, `mod_dir`, and `version` (check it against `nx::kModApiVersion`).

Guest addresses are host addresses, so `nx::ld<T>(addr)` and `nx::st<T>(addr, v)` from `nx/cpu.h` read and write
game memory directly. A replacement has the recompiled functions' signature, `void (nx::CPU&)`: arguments are in
`c.x[0..7]` and `c.v[...]`, and the result goes back in `c.x[0]`, as in AArch64.

**What can be replaced:** every function the game reaches through a function pointer. That covers virtual functions,
callbacks, and the enum and UI tables (this is how `games/wonder/menu.cpp` adds the recomp's menu entries). A direct
call inside the recompiled code is compiled to a direct C++ call and does not see the hook. To reach such a callee,
hook a caller that is reached through a pointer.

### Example: `examples/mods/hello`

`hello.cpp` logs every choice made in the pause menu, then lets the game act on it:

```cpp
nx::Fn g_game;
void pause_choice(nx::CPU& c) {
    g_api->log("hello: pause menu choice %s", *reinterpret_cast<const char* const*>(c.x[1]));
    g_game(c);
}
extern "C" NX_MOD_EXPORT void nx_mod_init(const nx::ModApi* api) {
    g_api = api;
    g_game = api->hook(0x1d3415c, pause_choice);  // game::sequence::PauseResult's from-string callback
}
```

The CMake build makes it as `<build>/mods/hello/hello.dll` (target `mod_hello`). To use it, copy that folder into
`<data root>/mods/`. A release ships it as `mods\-hello\` (off): rename the folder to `hello` to turn it on. Build
your own mod the same way, as a shared library with `runtime/include` on its include path, using the same compiler
as the recomp (clang-cl on Windows).

## ExeFS patches

Switch `.ips` patches (IPS and IPS32) are named after the build id of the module they patch. Only patches for
`main` apply, and they are recognised by the first 8 bytes of its build id. Offsets count from the start of the NSO
file, as Atmosphère reads them. Bytes that fall in main's data are written at start. Bytes that fall in main's code
are skipped and reported, because the code runs as recompiled C++. A code patch needs a recompile of
`private/wonder/exefs/main` with the patch applied (`python -m recomp`).

# SC4 Time Of Day Menu

Right click the **God Mode** button in SimCity 4: a small Maxis-style menu
opens next to it with the 24 hours in two columns (00:00–11:00 | 12:00–23:00).
Click an hour to set the city's time of day. The plugin issues the game's own
cheat, exactly as Ctrl+X `whattimeizit <hour>` does.

The menu closes after a pick, on a left click outside it, or on any right click.

## Install

Download `SC4TimeOfDayMenu-1.0.zip` from the
[Releases](https://github.com/Spamoidutout/SC4TimeOfDayMenu/releases) page and copy
`SC4TimeOfDayMenu.dll` and `SC4TimeOfDayMenu.dat` into the root of
`Documents\SimCity 4\Plugins`. Requires SimCity 4 1.1.641 for Windows.

## How it works

- Subscribes once, in `PostAppInit`, to the mouse message `0xFA0C6390`.
- On a right button release over the God Mode button (window `0x2988BC85`,
  found with `GetChildWindowFromIDRecursive`, visible), notes the request.
- In the `OnIdle` of a system service: opens the menu from the UI script
  `{0x00000000, 0xB99FCED4, 0x1}`, fills its texts; on a pick, closes it and
  calls `DoesCheatCodeMatch("whattimeizit")` then
  `SendCheatNotifications("whattimeizit <hour>", id)` on the cheat code manager.
- The menu only opens with a city loaded and closes if the city goes away.

## Build

```
sh build/build.sh
```

clang + lld-link, 32-bit, no CRT (`/nodefaultlib /noentry`), no imports; the
`.dat` is rebuilt by `tools/build_dat.py` (Python 3 + Pillow).

With Visual Studio: Win32 (x86) DLL, C++20, include dirs `sdk/include`,
`sdk/shim`, `src`, source `src/TimeOfDayMenu.cpp`. Add `src/NoCrt.cpp` only
for a build without the CRT (`/NODEFAULTLIB /NOENTRY /GS-`).

The `.dat` ids are in both `tools/build_dat.py` and `src/TimeOfDayMenu.cpp`.

## Files

| Path | What |
|---|---|
| `src/TimeOfDayMenu.cpp` | The plugin |
| `src/FixedString.h` | A `cIGZString` with no heap |
| `src/NoCrt.cpp` | `memset`, `memcpy`, `_purecall` for the no-CRT build |
| `sdk/` | gzcom-dll headers used (LGPL 2.1) |
| `tools/build_dat.py` | Builds the `.dat` |
| `assets/maxis_dialog_source.png` | Game dialog the frame and panel are cut from |
| `dist/` | Build output (not in the repository) |

## License

MIT, see `LICENSE`. The gzcom-dll headers are LGPL 2.1; the interface images
come from SimCity 4 (Electronic Arts / Maxis).

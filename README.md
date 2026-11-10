# SC4 Time Control

A SimCity 4 DLL plugin for the time of day: the more advanced version of
[SC4 Time Of Day Menu](https://github.com/Spamoidutout/SC4TimeOfDayMenu), with
a clock, cycle speeds and settings.

- **Clock**: the time is shown next to the date, in the bottom-left panel
  (in steps of 5 minutes, 10 at x8).
- **Menu**: right-click the God Mode button. Pick an hour (00:00–23:00), or the
  speed of the day/night cycle: Pause, 1/2, x1 (the game's normal speed), x2,
  x4, x8. The current speed is shown in red.

Setting an hour issues the game's own cheat, exactly as Ctrl+X
`whattimeizit <hour>` does. Pause only stops the clock if it is not already
stopped by the `stopwatch` cheat, and only undoes its own pause.

## SC4 Time Control or SC4 Time Of Day Menu?

- **SC4 Time Of Day Menu** is the lightweight one: right-click, pick an hour,
  done.
- **SC4 Time Control** (this one) is the more advanced version: everything the
  other one does, plus the clock next to the date, the cycle speeds and the
  settings.

Install one or the other, **not both**: they use the same button.

## Install

Download `SC4TimeControl-1.0.zip` from the
[Releases](https://github.com/Spamoidutout/SC4TimeControl/releases) page and copy
`SC4TimeControl.dll`, `SC4TimeControl.dat` and `SC4TimeControl.ini` into the root
of `Documents\SimCity 4\Plugins`. Requires SimCity 4 1.1.641 for
Windows.

## Settings (`SC4TimeControl.ini`)

| Key | Default | |
|---|---|---|
| `DefaultSpeed` | `x1` | Cycle speed when a city is loaded: `1/2`, `x1`, `x2`, `x4` or `x8` |
| `RememberLastSpeed` | `0` | `1`: the last speed picked in the menu becomes `DefaultSpeed` (written to the file) |

## How it works

- Mouse message `0xFA0C6390` (subscribed once in `PostAppInit`); right button
  release over the God Mode button (`0x2988BC85`, visible) opens the menu.
- Everything else runs in the `OnIdle` of a system service.
- Hours: `DoesCheatCodeMatch("whattimeizit")`, then
  `SendCheatNotifications("whattimeizit <hour>", id)`.
- Speeds: `cISC4City::Get24HourClock()`, then `SetTimeMagnification` (normal
  135, so 1/2 = 67.5 … x8 = 1080) and `Pause(true/false)` (a counter).
- Clock: our own window, child of the game's panel `0x0987B48F` at (149,128):
  `clock_field.png` (the date field's right end, widened) and a text that gets
  the font style of the game's date text (`GetFontStyle` / `SetFontStyle`).
  The game's date is not modified.

## Build

```
sh build/build.sh
```

clang + lld-link, 32-bit, no CRT (`/nodefaultlib /noentry`), imports only the
INI functions of kernel32; the `.dat` is built by `tools/build_dat.py`
(Python 3 + Pillow). Output in `dist/`.

With Visual Studio: Win32 (x86) DLL, C++20, include dirs `sdk/include`,
`sdk/shim`, `src`, source `src/TimeControl.cpp`, link `kernel32.lib`. Add
`src/NoCrt.cpp` only for a build without the CRT (`/NODEFAULTLIB /NOENTRY /GS-`).

The `.dat` ids are in both `tools/build_dat.py` and `src/TimeControl.cpp`.

## Files

| Path | What |
|---|---|
| `src/TimeControl.cpp` | The plugin |
| `src/FixedString.h` | A `cIGZString` with no heap |
| `src/NoCrt.cpp` | `memset`, `memcpy`, `_purecall`, `_fltused` for the no-CRT build |
| `SC4TimeControl.ini` | Default settings |
| `sdk/` | gzcom-dll headers used (LGPL 2.1) |
| `tools/build_dat.py` | Builds the `.dat` |
| `assets/maxis_dialog_source.png` | Game dialog the menu's frame and panel are cut from |
| `assets/clock_field.png` | The widened end of the date field |
| `build/` | `build.sh`, `kernel32.def` |

## License

MIT, see `LICENSE`. The gzcom-dll headers are LGPL 2.1; the interface images
come from SimCity 4 (Electronic Arts / Maxis).
